#include <windows.h>

#include "Platform.h"
#include "../Sources/HistorySync/vendor/json.hpp"
#include "ConfigRewrite.h"
#include "HistorySync.h"
#include "Diagnostics.h"
#include "OperationGuard.h"
#include "ConfigSources.h"
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
std::string windowsFailure(const char *action, DWORD code) {
  LPWSTR detail = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, code, 0, reinterpret_cast<LPWSTR>(&detail), 0, nullptr);
  std::string result = std::string(action) + "（Windows 错误码 " + std::to_string(code) + "）";
  if (detail) { result += "：" + utf8(detail); LocalFree(detail); }
  return result;
}
void atomic(const fs::path &p, const std::string &data, bool replace = true) {
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
  if (file == INVALID_HANDLE_VALUE)
    throw std::runtime_error(windowsFailure("无法创建临时配置文件", GetLastError()));
  DWORD written = 0;
  bool ok =
      data.size() <= MAXDWORD &&
      WriteFile(file, data.data(), DWORD(data.size()), &written, nullptr) &&
      written == data.size() && FlushFileBuffers(file);
  DWORD failureCode = ok ? ERROR_SUCCESS : GetLastError();
  CloseHandle(file);
  if (ok && !MoveFileExW(tmp.c_str(), p.c_str(),
                         (replace ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH)) {
    failureCode = GetLastError();
    ok = false;
  }
  if (!ok) {
    std::error_code ignored;
    fs::remove(tmp, ignored);
    throw std::runtime_error(windowsFailure("无法写入连接配置", failureCode));
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
using Buffer = std::unique_ptr<char, decltype(&yilai_config_free)>;
struct OperationLog {
  YilaiDiagnostic *context;
  std::string stage = "start";
  std::string label = "开始操作";
  void step(const char *code, const char *description) {
    stage = code;
    label = description;
    yilai_diagnostic_event(context, code, description);
  }
};
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
static std::wstring perform(Action action, const fs::path &root,
                            const std::wstring &input, bool closed,
                            OperationLog &log, const fs::path &runtimeOverride, bool &historyWarning) {
  bool historyReady = true;
  log.step("lock_operation", "检查其他配置器操作");
  char *lockError = nullptr;
  std::unique_ptr<YilaiOperationLock, decltype(&yilai_operation_unlock)> operationLock(
      yilai_operation_lock(utf8(root.wstring()).c_str(), &lockError), yilai_operation_unlock);
  Buffer lockDetail(lockError, yilai_config_free);
  ensure(operationLock != nullptr, lockDetail ? lockDetail.get() : "无法取得操作锁");
  log.step("checking_apps", "检查后台程序");
  if (closed)
    requireAppsClosed();
  const auto config = root / L"config.toml";
  if (action == Action::Cleanup) {
    log.step("rename_config", "停用旧配置");
    regular(config);
    if (!fs::exists(config))
      return L"没有需要重置的配置。填写 API Key 后即可切换。";
    auto disabled = config;
    disabled += L".disabled-" +
                std::to_wstring(std::chrono::high_resolution_clock::now()
                                    .time_since_epoch()
                                    .count());
    if (!MoveFileW(config.c_str(), disabled.c_str()))
      throw std::runtime_error(windowsFailure("无法停用旧配置", GetLastError()));
    return L"旧配置已停用。填写 API Key 后可重新切换。";
  }
  if (action == Action::Sync || action == Action::Undo) {
    log.step("sync_history", "处理本地历史");
    char *error = nullptr;
    Buffer result(yilai_sync_history(utf8(root.wstring()).c_str(),
                                     action == Action::Undo, &error),
                  yilai_config_free);
    Buffer detail(error, yilai_config_free);
    ensure(result != nullptr,
           detail ? detail.get() : "History operation failed");
    return action == Action::Sync ? L"本地历史已同步。" : L"已撤销历史同步。";
  }
  if (action == Action::Configure) {
    log.step("recover_history", "恢复上次未完成的历史操作");
    char *recoveryError = nullptr;
    Buffer recovered(yilai_recover_history(utf8(root.wstring()).c_str(), &recoveryError), yilai_config_free);
    Buffer recoveryDetail(recoveryError, yilai_config_free);
    if (!recovered) {
      historyReady = false;
      historyWarning = true;
      yilai_diagnostic_event(log.context, "history_warning", recoveryDetail ? recoveryDetail.get() : "历史恢复未完成");
    }
  }
  std::unique_ptr<YilaiConfigSources, decltype(&yilai_sources_finish)> sources(nullptr, yilai_sources_finish);
  if ((action == Action::Configure) && (closed || !runtimeOverride.empty())) {
    log.step("inspect_sources", "识别当前生效来源");
    char *sourceError = nullptr;
    sources.reset(yilai_sources_prepare(utf8(root.wstring()).c_str(), utf8(runtimeOverride.wstring()).c_str(), &sourceError));
    Buffer sourceDetail(sourceError, yilai_config_free);
    ensure(sources != nullptr, sourceDetail ? sourceDetail.get() : "无法识别配置来源");
    Buffer summary(yilai_sources_summary(sources.get()), yilai_config_free);
    yilai_diagnostic_event(log.context, "source_plan", summary ? summary.get() : "来源计划已生成");
  }
  log.step("prepare_config", "准备连接配置");
  regular(config);
  const bool hadConfig = fs::exists(config);
  const auto before = hadConfig ? read(config) : "";
  ensure(before.find('\0') == std::string::npos,
         "Configuration contains NUL bytes");
  std::wstring key = input;
  auto start = key.find_first_not_of(L" \t\r\n");
  key = start == std::wstring::npos
            ? L""
            : key.substr(start, key.find_last_not_of(L" \t\r\n") - start + 1);
  ensure(key.find(L'\0') == std::wstring::npos, "API key contains NUL bytes");
  const int operation = action == Action::Images     ? YILAI_ENHANCE
                                                     : YILAI_CONFIGURE;
  char *error = nullptr;
  Buffer result(
      yilai_apply_config(before.c_str(), utf8(key).c_str(), operation, &error),
      yilai_config_free);
  Buffer detail(error, yilai_config_free);
  ensure(result != nullptr,
         detail ? detail.get() : "Configuration update failed");
  const std::string after(result.get());
  ensure((fs::exists(config) ? read(config) : "") == before,
         "配置已被其他程序改动，请关闭后重试。");
  if (action == Action::Images) {
    atomic(config, after);
    return L"生图已启用。";
  }
  log.step("read_auth", "检查登录文件");
  const auto authPath = root / L"auth.json";
  regular(authPath);
  const bool hadAuth = fs::exists(authPath);
  const auto authBefore = hadAuth ? read(authPath) : "";
  bool wroteConfig = false, removedAuth = false;
  try {
    if (sources) {
      log.step("clear_sources", "清除旧连接覆盖");
      char *sourceError = nullptr;
      auto ok = yilai_sources_apply(sources.get(), &sourceError);
      Buffer sourceDetail(sourceError, yilai_config_free);
      ensure(ok != 0, sourceDetail ? sourceDetail.get() : "来源清理失败");
    }
    log.step("write_config", "写入连接配置");
    ensure(fs::exists(config) == hadConfig && (hadConfig ? read(config) : "") == before,
           "配置已被其他程序改动，请关闭后重试。");
    atomic(config, after);
    wroteConfig = true;
    log.step("delete_auth", "删除旧登录文件");
    ensure(fs::exists(authPath) == hadAuth && (hadAuth ? read(authPath) : "") == authBefore,
           "登录文件已被其他程序改动，请关闭后重试。");
    if (hadAuth) {
      if (!DeleteFileW(authPath.c_str()))
        throw std::runtime_error(windowsFailure("无法删除登录文件", GetLastError()));
      removedAuth = true;
    }
    ensure(!fs::exists(authPath),
           "登录文件被重新创建，请完全退出 Codex 和 CC-Switch。");
    if (sources) {
      log.step("verify_sources", "核验新连接实际生效");
      char *sourceError = nullptr;
      auto ok = yilai_sources_verify(sources.get(), utf8(key).c_str(), &sourceError);
      Buffer sourceDetail(sourceError, yilai_config_free);
      ensure(ok != 0, sourceDetail ? sourceDetail.get() : "来源核验失败");
    }
  } catch (...) {
    const auto failedStage = log.stage, failedLabel = log.label;
    std::string originalError = "未知错误";
    try { throw; } catch (const std::exception &e) { originalError = e.what(); } catch (...) {}
    yilai_diagnostic_event(log.context, "switch_failed", originalError.c_str());
    bool restored = true;
    log.step("rollback_config", "还原连接配置");
    try {
      if (wroteConfig) {
        ensure(fs::exists(config) && read(config) == after,
               "配置已被外部修改，未覆盖外部改动。");
        if (hadConfig)
          atomic(config, before);
        else
          fs::remove(config);
      }
      yilai_diagnostic_event(log.context, "rollback_config", "还原完成");
    } catch (const std::exception &e) {
      restored = false;
      yilai_diagnostic_event(log.context, "rollback_config", e.what());
    }
    log.step("rollback_auth", "还原登录文件");
    try {
      if (removedAuth) {
        ensure(!fs::exists(authPath), "登录文件已由外部重新创建，未覆盖外部改动。");
        atomic(authPath, authBefore, false);
      }
      yilai_diagnostic_event(log.context, "rollback_auth", "还原完成");
    } catch (const std::exception &e) {
      restored = false;
      yilai_diagnostic_event(log.context, "rollback_auth", e.what());
    }
    if (sources) {
      log.step("rollback_sources", "还原配置来源");
      char *sourceError = nullptr;
      auto ok = yilai_sources_rollback(sources.get(), &sourceError);
      Buffer sourceDetail(sourceError, yilai_config_free);
      if (!ok) restored = false;
      yilai_diagnostic_event(log.context, "rollback_sources", ok ? "还原完成" : (sourceDetail ? sourceDetail.get() : "来源恢复失败"));
    }
    log.step(failedStage.c_str(), failedLabel.c_str());
    if (!restored)
      throw std::runtime_error("切换未完成且回滚不完整，请保留历史备份并联系支持。原始原因：" + originalError);
    throw;
  }
  if (historyReady) {
    log.step("sync_history", "同步本地历史");
    char *syncError = nullptr;
    Buffer synced(yilai_sync_history(utf8(root.wstring()).c_str(), 0, &syncError), yilai_config_free);
    Buffer syncDetail(syncError, yilai_config_free);
    if (!synced) {
      historyWarning = true;
      yilai_diagnostic_event(log.context, "history_warning", syncDetail ? syncDetail.get() : "历史同步未完成");
    }
  }
  return historyWarning
    ? L"API 已配置，生图已启用；历史同步未完成，请查看日志。请重开 Codex。"
    : L"API 已配置，生图和本地历史已就绪。请重开 Codex。";
}
std::wstring run(Action action, const fs::path &root, const std::wstring &input,
                 bool closed, const fs::path &runtimeOverride, bool *historyWarning) {
  bool warning = false;
  if (historyWarning) *historyWarning = false;
  const char *name = action == Action::Configure  ? "switch_api"
                     : action == Action::Cleanup  ? "reset_config"
                     : action == Action::Undo     ? "undo_history"
                     : action == Action::Sync     ? "sync_history"
                                                  : "enhance_images";
  OperationLog log{yilai_diagnostic_begin(utf8(root.wstring()).c_str(), name,
                                          utf8(input).c_str())};
  try {
    auto result = perform(action, root, input, closed, log, runtimeOverride, warning);
    if (historyWarning) *historyWarning = warning;
    yilai_diagnostic_end(log.context, 1, warning ? "API configured; history incomplete" : "操作完成");
    return result;
  } catch (const std::exception &error) {
    Buffer clean(yilai_diagnostic_sanitize(log.context, error.what()),
                 yilai_config_free);
    std::string message =
        log.label + "失败：" + (clean ? clean.get() : "错误详情不可用");
    yilai_diagnostic_event(log.context, log.stage.c_str(), message.c_str());
    if (!yilai_diagnostic_finish(log.context, 0, message.c_str()))
      message += " 诊断日志未能完整保存，请保留当前错误信息。";
    throw std::runtime_error(message);
  } catch (...) {
    yilai_diagnostic_end(log.context, 0, "未知错误");
    throw std::runtime_error("操作未完成，发生未知错误。");
  }
}
bool selfTest(std::wstring &error) {
  try {
    for (auto test : {yilai_config_self_test, yilai_history_self_test,
                      yilai_diagnostic_self_test}) {
      char *detail = nullptr;
      int ok = test(&detail);
      Buffer message(detail, yilai_config_free);
      ensure(ok != 0, message ? message.get() : "Shared self-test failed");
    }
    const auto parent =
        fs::absolute(fs::temp_directory_path()).lexically_normal();
    const auto root =
        parent / (L"YilaiSwitcher-v331-" +
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
    wchar_t *previousEnv = nullptr;
    size_t envLength = 0;
    _wdupenv_s(&previousEnv, &envLength, L"CODEX_SQLITE_HOME");
    const bool hadSqliteEnv = previousEnv != nullptr;
    const std::wstring previousSqlite = previousEnv ? previousEnv : L"";
    free(previousEnv);
    _wputenv_s(L"CODEX_SQLITE_HOME", root.c_str());
    struct RestoreEnv {
      bool had;
      std::wstring value;
      ~RestoreEnv() {
        _wputenv_s(L"CODEX_SQLITE_HOME", had ? value.c_str() : L"");
      }
    } restoreEnv{hadSqliteEnv, previousSqlite};
    const std::string config =
        "model='gpt-6-astra'\nmodel_provider='custom'\nmodel_catalog_json='cc-"
        "switch-model-catalog.json'\n[model_providers.custom]\nname='Other'"
        "\nbase_url='https://other.invalid'\nwire_api='responses'\n";
    const std::string auth = "{\"auth_mode\":\"chatgpt\"}";
    const auto noReplacePath = root / L"no-replace-auth.json";
    atomic(noReplacePath, "existing-auth");
    bool collisionRejected = false;
    try { atomic(noReplacePath, "replacement-auth", false); }
    catch (...) { collisionRejected = true; }
    ensure(collisionRejected && read(noReplacePath) == "existing-auth",
           "Authentication restore overwrote an existing file");
    fs::remove(noReplacePath);
    atomic(noReplacePath, "restored-auth", false);
    ensure(read(noReplacePath) == "restored-auth", "No-replace auth restoration failed");
    fs::remove(noReplacePath);
    atomic(root / L"config.toml", config);
    atomic(root / L"auth.json", auth);
    char *lockFailure = nullptr;
    auto held = yilai_operation_lock(utf8(root.wstring()).c_str(), &lockFailure);
    Buffer lockFailureText(lockFailure, yilai_config_free);
    ensure(held != nullptr, "Cannot establish isolated operation lock");
    bool rejected = false;
    try { run(Action::Configure, root, L"sk-test", false); }
    catch (const std::exception &e) { rejected = std::string(e.what()).find("另一个配置器") != std::string::npos; }
    yilai_operation_unlock(held);
    ensure(rejected && read(root / L"config.toml") == config && read(root / L"auth.json") == auth,
           "Overlapping operation was not rejected without changes");
    HANDLE locked = CreateFileW((root / L"auth.json").c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ensure(locked != INVALID_HANDLE_VALUE, "Cannot lock test auth");
    bool failed = false;
    try {
      run(Action::Configure, root, L"sk-test", false);
    } catch (...) {
      failed = true;
    }
    CloseHandle(locked);
    ensure(failed && read(root / L"config.toml") == config &&
               read(root / L"auth.json") == auth,
           "Auth deletion failure did not roll back configuration");
    atomic(root / L"sessions/bad.jsonl", "invalid history\n");
    bool warning = false;
    auto partial = run(Action::Configure, root, L"sk-test", false, {}, &warning);
    ensure(warning && mode(root) == L"易来 API" && !fs::exists(root / L"auth.json") && partial.find(L"历史同步未完成") != std::wstring::npos,
           "History failure must preserve successful API configuration and report warning");
    fs::remove(root / L"sessions/bad.jsonl");
    atomic(root / L"sessions/example.jsonl",
           "{\"type\":\"session_meta\",\"payload\":{\"id\":\"example\",\"model_"
           "provider\":\"openai\"}}\n{\"type\":\"event_msg\",\"payload\":{"
           "\"message\":\"keep\"}}\n");
    run(Action::Configure, root, L"sk-test", false);
    ensure(mode(root) == L"易来 API" && !fs::exists(root / L"auth.json"),
           "API switch did not remove auth");
    ensure(read(root / L"sessions/example.jsonl").find("custom") !=
               std::string::npos,
           "Switch did not synchronize history");
    atomic(root / L"auth.json", auth);
    const std::string broken = "invalid=[configuration";
    atomic(root / L"config.toml", broken);
    atomic(root / L"auth.json", auth);
    const auto historyBefore = read(root / L"sessions/example.jsonl");
    run(Action::Cleanup, root, L"", false);
    ensure(!fs::exists(root / L"config.toml") &&
               read(root / L"auth.json") == auth &&
               read(root / L"sessions/example.jsonl") == historyBefore,
           "Reset changed auth or history");
    size_t preserved = 0;
    for (const auto &item : fs::directory_iterator(root))
      if (item.path().filename().wstring().rfind(L"config.toml.disabled-", 0) ==
              0 &&
          read(item.path()) == broken)
        ++preserved;
    ensure(preserved == 1, "Reset did not preserve configuration bytes");
    run(Action::Cleanup, root, L"", false);
    std::string diagnostics;
    for (const auto &entry :
         fs::directory_iterator(root / L"yilai-switcher-logs"))
      if (entry.is_regular_file())
        diagnostics += read(entry.path());
    ensure(diagnostics.find("sk-test") == std::string::npos,
           "Diagnostics leaked API key");
    ensure(diagnostics.find("rollback_config") != std::string::npos &&
               diagnostics.find("delete_auth") != std::string::npos,
           "Diagnostics omitted failed stage or rollback");
    return true;
  } catch (const std::exception &e) {
    error = wide(e.what());
    return false;
  }
}
} // namespace app
