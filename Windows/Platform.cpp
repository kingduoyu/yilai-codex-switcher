#include <windows.h>

#include "Platform.h"
#include "../Sources/HistorySync/vendor/json.hpp"
#include "ConfigRewrite.h"
#include "HistorySync.h"
#include <chrono>
#include <fstream>
#include <memory>
#include <shlobj.h>
#include <shobjidl.h>
#include <sstream>
#include <tlhelp32.h>
#include <vector>
#include <wrl/client.h>
namespace app {
namespace fs = std::filesystem;
namespace {
std::string utf8(const std::wstring &s) {
  if (s.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0,
                              nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n,
                      nullptr, nullptr);
  return out;
}
std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              int(s.size()), nullptr, 0);
  if (!n)
    throw std::runtime_error("Invalid UTF-8");
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
  return out;
}
void ensure(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
std::string read(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  ensure(bool(in), "Cannot read configuration");
  std::ostringstream out;
  out << in.rdbuf();
  ensure(!in.bad(), "Configuration read failed");
  return out.str();
}
void regular(const fs::path &p) {
  if (!fs::exists(p))
    return;
  DWORD flags = GetFileAttributesW(p.c_str());
  ensure(fs::is_regular_file(p) && !(flags & FILE_ATTRIBUTE_REPARSE_POINT),
         "Expected a regular file, not a link");
}
void atomic(const fs::path &p, const std::string &data) {
  regular(p);
  fs::create_directories(p.parent_path());
  auto tmp = p;
  tmp +=
      L".write-" +
      std::to_wstring(
          std::chrono::high_resolution_clock::now().time_since_epoch().count());
  HANDLE file =
      CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  ensure(file != INVALID_HANDLE_VALUE, "Cannot create temporary configuration");
  DWORD written = 0;
  bool ok =
      data.size() <= MAXDWORD &&
      WriteFile(file, data.data(), DWORD(data.size()), &written, nullptr) &&
      written == data.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (!ok || !MoveFileExW(tmp.c_str(), p.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::error_code ignored;
    fs::remove(tmp, ignored);
    throw std::runtime_error("Atomic configuration write failed");
  }
}
void requireAppsClosed() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  ensure(snapshot != INVALID_HANDLE_VALUE, "Cannot check running applications");
  PROCESSENTRY32W process{};
  process.dwSize = sizeof(process);
  bool running = false;
  if (Process32FirstW(snapshot, &process))
    do {
      if (_wcsicmp(process.szExeFile, L"Codex.exe") == 0 ||
          _wcsicmp(process.szExeFile, L"cc-switch.exe") == 0)
        running = true;
    } while (Process32NextW(snapshot, &process));
  CloseHandle(snapshot);
  ensure(
      !running,
      "请完全退出 Codex 和 CC-Switch 后再操作；关闭窗口后也请检查后台进程。");
}
void recycle(const fs::path &input) {
  auto p = input.lexically_normal();
  p.make_preferred();
  Microsoft::WRL::ComPtr<IFileOperation> op;
  Microsoft::WRL::ComPtr<IShellItem> item;
  ensure(SUCCEEDED(CoCreateInstance(CLSID_FileOperation, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(op.GetAddressOf()))),
         "Cannot initialize Recycle Bin");
  auto check = [&](HRESULT code, const char *step) {
    if (FAILED(code))
      throw std::runtime_error(
          std::string("Recycle Bin ") + step + " failed (" +
          std::to_string(static_cast<unsigned long>(code)) +
          "): " + utf8(p.wstring()));
  };
  check(op->SetOperationFlags(FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE |
                              FOF_NO_UI),
        "flags");
  check(SHCreateItemFromParsingName(p.c_str(), nullptr,
                                    IID_PPV_ARGS(item.GetAddressOf())),
        "item");
  check(op->DeleteItem(item.Get(), nullptr), "queue");
  check(op->PerformOperations(), "perform");
  BOOL aborted = FALSE;
  check(op->GetAnyOperationsAborted(&aborted), "result");
  ensure(!aborted && !fs::exists(p), "Recycle operation was not completed");
}
void apply(const fs::path &root, const std::string &before,
           const std::string &after, bool cleanup) {
  auto config = root / L"config.toml";
  ensure((fs::exists(config) ? read(config) : "") == before,
         "Configuration changed; retry after closing other tools");
  if (!cleanup) {
    atomic(config, after);
    return;
  }
  struct Snapshot {
    fs::path path;
    bool existed;
    std::string bytes;
  };
  std::vector<Snapshot> snapshots;
  for (const auto &target : std::vector<fs::path>{
           config, root / L"auth.json", root / L"auth.json.yilai-disabled",
           root / L"yilai-switcher-backup/manifest.json",
           root / L"yilai-switcher-backup/config.toml"}) {
    regular(target);
    bool exists = fs::exists(target);
    snapshots.push_back({target, exists, exists ? read(target) : ""});
  }
  std::vector<size_t> changed;
  try {
    atomic(config, after);
    changed.push_back(0);
    for (size_t i = 1; i < snapshots.size(); ++i)
      if (snapshots[i].existed) {
        recycle(snapshots[i].path);
        changed.push_back(i);
      }
    ensure(!fs::exists(root / L"auth.json") &&
               !fs::exists(root / L"auth.json.yilai-disabled"),
           "Authentication file was recreated during cleanup");
  } catch (...) {
    bool restored = true;
    for (auto it = changed.rbegin(); it != changed.rend(); ++it)
      try {
        const auto &snapshot = snapshots[*it];
        if (snapshot.existed)
          atomic(snapshot.path, snapshot.bytes);
        else
          fs::remove(snapshot.path);
      } catch (...) {
        restored = false;
      }
    ensure(restored, "Cleanup rollback incomplete; keep Recycle Bin backups");
    throw;
  }
}
using Buffer = std::unique_ptr<char, decltype(&yilai_config_free)>;
} // namespace
fs::path home() {
  wchar_t *env = nullptr;
  size_t n = 0;
  if (_wdupenv_s(&env, &n, L"CODEX_HOME") == 0 && env) {
    std::wstring value(env);
    free(env);
    if (!value.empty())
      return fs::absolute(value).lexically_normal();
  }
  PWSTR profile = nullptr;
  ensure(
      SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile)),
      "Cannot locate home directory");
  fs::path root(profile);
  CoTaskMemFree(profile);
  return root / L".codex";
}
std::wstring mode(const fs::path &root) {
  auto file = root / L"config.toml";
  if (!fs::exists(file))
    return L"尚未配置";
  switch (yilai_config_mode(read(file).c_str())) {
  case 0:
    return L"OpenAI 官方";
  case 1:
    return L"易来 API";
  case 2:
    return L"其他 CCS / 第三方连接";
  default:
    return L"配置需要检查";
  }
}
std::wstring run(Action action, const fs::path &root, const std::wstring &input,
                 bool closed) {
  if (closed)
    requireAppsClosed();
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ensure(SUCCEEDED(com), "COM initialization failed");
  struct Scope {
    ~Scope() { CoUninitialize(); }
  } scope;
  if (action == Action::Sync || action == Action::Undo) {
    char *error = nullptr;
    Buffer result(yilai_sync_history(utf8(root.wstring()).c_str(),
                                     action == Action::Undo, &error),
                  yilai_config_free);
    Buffer detail(error, yilai_config_free);
    ensure(result != nullptr,
           detail ? detail.get() : "History operation failed");
    auto report = nlohmann::json::parse(result.get());
    std::wstring message =
        action == Action::Undo ? L"已撤销上次同步：" : L"已同步本地历史：";
    message += std::to_wstring(report["files"].get<size_t>()) +
               L" 个会话文件，" +
               std::to_wstring(report["rows"].get<size_t>()) + L" 条索引。";
    if (!report["backup"].get<std::string>().empty())
      message += L"\r\n备份：" + wide(report["backup"]);
    return message;
  }
  const auto config = root / L"config.toml";
  regular(config);
  const auto before = fs::exists(config) ? read(config) : "";
  ensure(before.find('\0') == std::string::npos,
         "Configuration contains NUL bytes");
  std::wstring key = input;
  auto begin = key.find_first_not_of(L" \t\r\n");
  key = begin == std::wstring::npos
            ? L""
            : key.substr(begin, key.find_last_not_of(L" \t\r\n") - begin + 1);
  ensure(key.find(L'\0') == std::wstring::npos, "API key contains NUL bytes");
  int operation = action == Action::Images      ? YILAI_ENHANCE
                  : action == Action::Configure ? YILAI_CONFIGURE
                                                : YILAI_CLEANUP;
  char *error = nullptr;
  Buffer result(
      yilai_apply_config(before.c_str(), utf8(key).c_str(), operation, &error),
      yilai_config_free);
  Buffer detail(error, yilai_config_free);
  ensure(result != nullptr,
         detail ? detail.get() : "Configuration update failed");
  apply(root, before, result.get(), action == Action::Cleanup);
  return action == Action::Images
             ? L"生图已启用；模型、目录和登录保持不变。请重开 Codex。"
         : action == Action::Configure
             ? L"易来连接已配置；保留现有模型和登录。请重开 Codex。"
             : L"旧登录已移入回收站；已解除本工具旧模型目录引用。";
}
bool selfTest(std::wstring &error) {
  try {
    for (auto test : {yilai_config_self_test, yilai_history_self_test}) {
      char *detail = nullptr;
      int ok = test(&detail);
      Buffer message(detail, yilai_config_free);
      ensure(ok != 0, message ? message.get() : "Shared self-test failed");
    }
    auto parent = fs::absolute(fs::temp_directory_path()).lexically_normal();
    auto root =
        parent / (L"YilaiCodexSwitcher-cpp-" +
                  std::to_wstring(std::chrono::high_resolution_clock::now()
                                      .time_since_epoch()
                                      .count()));
    ensure(root.parent_path() == parent, "Unsafe test directory");
    fs::create_directories(root);
    struct Cleanup {
      fs::path p;
      ~Cleanup() {
        std::error_code ignored;
        fs::remove_all(p, ignored);
      }
    } cleanup{root};
    const std::string config =
        "model='gpt-6-astra'\nmodel_provider='custom'\nmodel_catalog_json='cc-"
        "switch-model-catalog.json'\n[model_providers.custom]\nname='Other'"
        "\nbase_url='https://"
        "other.invalid'\nwire_api='responses'\n[model_providers.custom.http_"
        "headers]\nX-Keep='yes'\n";
    const std::string auth = "{\"auth_mode\":\"chatgpt\"}";
    atomic(root / L"config.toml", config);
    atomic(root / L"auth.json", auth);
    atomic(root / L"auth.json.yilai-disabled", auth);
    atomic(root / L"yilai-switcher-backup/manifest.json", "synthetic manifest");
    atomic(root / L"yilai-switcher-backup/config.toml", "synthetic config");
    ensure(mode(root) == L"其他 CCS / 第三方连接",
           "Custom falsely identified as Yilai");
    run(Action::Images, root, L"", false);
    ensure(read(root / L"auth.json") == auth, "Default operation changed auth");
    auto once = read(root / L"config.toml");
    run(Action::Images, root, L"", false);
    ensure(read(root / L"config.toml") == once, "Enhancement not idempotent");
    run(Action::Configure, root, L"sk-test-key", false);
    ensure(mode(root) == L"易来 API" && read(root / L"auth.json") == auth,
           "Explicit configure changed credentials");
    auto before = read(root / L"config.toml");
    HANDLE locked =
        CreateFileW((root / L"auth.json.yilai-disabled").c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    ensure(locked != INVALID_HANDLE_VALUE, "Cannot lock synthetic file");
    bool failed = false;
    try {
      run(Action::Cleanup, root, L"", false);
    } catch (...) {
      failed = true;
    }
    CloseHandle(locked);
    ensure(failed && read(root / L"config.toml") == before &&
               read(root / L"auth.json") == auth,
           "Cleanup rollback failed");
    run(Action::Cleanup, root, L"", false);
    ensure(!fs::exists(root / L"auth.json") &&
               !fs::exists(root / L"auth.json.yilai-disabled"),
           "Cleanup incomplete");
    atomic(root / L"config.toml", "model='gpt-6-astra'\n");
    atomic(root / L"auth.json", auth);
    failed = false;
    try {
      run(Action::Cleanup, root, L"", false);
    } catch (...) {
      failed = true;
    }
    ensure(failed && read(root / L"auth.json") == auth,
           "Official credentials not protected");
    return true;
  } catch (const std::exception &e) {
    error = wide(e.what());
    return false;
  }
}
} // namespace app
