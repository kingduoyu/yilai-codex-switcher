#include "ConfigSources.h"
#include "ConfigRewrite.h"
#include "runtime_probe.h"
#include "../Shared/vendor/json.hpp"
#include "../ConfigRewrite/vendor/toml.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace {
void require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
std::string text(const fs::path &p) { return p.u8string(); }
char *copy(const std::string &s) {
  auto p = static_cast<char *>(std::malloc(s.size() + 1));
  if (p) std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}
void failure(char **error, const char *message) { if (error) *error = copy(message); }
bool same(const fs::path &a, const fs::path &b) {
  std::error_code ec;
  if (fs::equivalent(a, b, ec) && !ec) return true;
#ifdef _WIN32
  return _wcsicmp(fs::absolute(a).lexically_normal().c_str(), fs::absolute(b).lexically_normal().c_str()) == 0;
#else
  return fs::absolute(a).lexically_normal() == fs::absolute(b).lexically_normal();
#endif
}
void regular(const fs::path &p) {
#ifdef _WIN32
  auto attrs = GetFileAttributesW(p.c_str());
  require(attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT), "配置来源是链接，未自动修改：" + text(p));
#else
  require(!fs::is_symlink(fs::symlink_status(p)), "配置来源是链接，未自动修改：" + text(p));
#endif
  require(!fs::exists(p) || fs::is_regular_file(p), "配置来源不是普通文件：" + text(p));
}
std::string read(const fs::path &p) {
  regular(p);
  require(fs::is_regular_file(p) && fs::file_size(p) <= 8 * 1024 * 1024, "无法读取或配置文件过大：" + text(p));
  std::ifstream f(p, std::ios::binary); std::ostringstream out; out << f.rdbuf();
  require(bool(f) && !f.bad(), "配置读取失败：" + text(p));
  return out.str();
}
Json readJson(const fs::path &p) {
  try { return Json::parse(read(p)); }
  catch (const Json::exception &) { throw std::runtime_error("配置来源记录无法解析，未修改原文件：" + text(p)); }
}
std::string unique() {
  static std::atomic<unsigned> counter{0};
  return std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + "-" + std::to_string(counter++);
}
void write(const fs::path &p, const std::string &bytes) {
  regular(p); fs::create_directories(p.parent_path());
  auto tmp = p; tmp += ".yilai-source-tmp-" + unique();
  struct Cleanup { fs::path p; ~Cleanup() { std::error_code ec; fs::remove(p, ec); } } cleanup{tmp};
#ifdef _WIN32
  auto file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  require(file != INVALID_HANDLE_VALUE, "无法写入配置来源：" + text(p));
  DWORD done = 0;
  bool ok = WriteFile(file, bytes.data(), DWORD(bytes.size()), &done, nullptr) && done == bytes.size() && FlushFileBuffers(file);
  auto code = ok ? ERROR_SUCCESS : GetLastError(); CloseHandle(file);
  if (ok && !MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { code = GetLastError(); ok = false; }
  require(ok, "配置来源写入失败（Windows " + std::to_string(code) + "）：" + text(p));
#else
  auto file = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  require(file >= 0, "无法写入配置来源：" + text(p));
  size_t done = 0; bool ok = true;
  while (done < bytes.size()) { auto n = ::write(file, bytes.data() + done, bytes.size() - done); if (n < 0 && errno == EINTR) continue; if (n <= 0) { ok = false; break; } done += size_t(n); }
  ok = (::fsync(file) == 0) && ok; ::close(file);
  require(ok && ::rename(tmp.c_str(), p.c_str()) == 0, "配置来源写入失败：" + text(p));
#endif
}
void privateDirectory(const fs::path &p) {
  auto status = fs::symlink_status(p);
  require(!fs::is_symlink(status), "配置来源备份目录不能是链接。");
  fs::create_directories(p);
#ifndef _WIN32
  require(::chmod(p.c_str(), 0700) == 0, "无法设置配置来源备份权限。");
#endif
}
std::vector<fs::path> contexts(const fs::path &home) {
  std::vector<fs::path> result{home};
  auto statePath = home / ".codex-global-state.json";
  if (!fs::exists(statePath)) return result;
  auto state = readJson(statePath);
  if (!state.contains("active-workspace-roots")) return result;
  require(state["active-workspace-roots"].is_array(), "当前工作目录记录格式不受支持，未修改配置。");
  for (auto &value : state["active-workspace-roots"]) {
    require(value.is_string(), "当前工作目录记录格式不受支持，未修改配置。");
    auto path = fs::u8path(value.get<std::string>());
    require(path.is_absolute() && fs::is_directory(path), "当前项目目录不可在本机访问，请在对应主机配置：" + text(path));
    if (std::none_of(result.begin(), result.end(), [&](auto &item) { return same(item, path); })) result.push_back(path);
  }
  require(result.size() <= 16, "当前项目过多，请保留需要切换的活动项目后重试。");
  return result;
}
bool profilePathAllowed(const fs::path &file, const fs::path &home) {
  const auto name = file.filename().u8string();
  const std::string suffix = ".config.toml";
  return same(file.parent_path(), home) && name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}
bool projectPathAllowed(const fs::path &file, const std::vector<fs::path> &roots) {
  if (file.filename() != "config.toml" || file.parent_path().filename() != ".codex") return false;
  for (auto root : roots) for (;;) {
    if (same(root, file.parent_path().parent_path())) return true;
    auto parent = root.parent_path(); if (parent == root || parent.empty()) break; root = parent;
  }
  return false;
}
Json probe(const fs::path &runtime, const fs::path &home, const fs::path &cwd) {
  try {
    auto result = Json::parse(yilai_sources::probe_config(runtime, home, cwd));
    require(result.contains("config") && result["config"].is_object() && result.contains("layers") && result["layers"].is_array(), "Codex 未提供可识别的配置来源，未继续写入。");
    return result;
  } catch (const Json::exception &) { throw std::runtime_error("Codex 配置来源响应格式不受支持。"); }
}
struct Edit { fs::path path; std::string before, after; };
void restoreEdits(const std::vector<Edit> &edits) {
  std::vector<std::string> errors;
  for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
    try {
      auto now = read(it->path);
      if (now == it->before) continue;
      require(now == it->after, "来源已被外部改动，未覆盖：" + text(it->path));
      write(it->path, it->before);
    } catch (const std::exception &e) { errors.push_back(e.what()); }
  }
  if (!errors.empty()) throw std::runtime_error("配置来源恢复不完整，请保留备份。" + errors.front());
}
void recover(const fs::path &home) {
  auto parent = home / "yilai-source-backups"; auto pending = parent / "pending.json";
  if (!fs::exists(pending)) return;
  auto pointer = readJson(pending); auto generation = pointer.at("generation").get<std::string>();
  require(!generation.empty() && generation.find_first_not_of("0123456789-") == std::string::npos, "配置来源恢复记录无效。");
  auto folder = parent / generation; auto manifest = readJson(folder / "manifest.json");
  require(manifest.value("version", 0) == 1 && same(fs::u8path(manifest.at("home").get<std::string>()), home), "配置来源备份不属于当前目录。");
  auto status = manifest.value("status", "");
  if (status == "complete" || status == "rolled_back") { fs::remove(pending); return; }
  require(status == "pending" || status == "recovery_required", "未知的配置来源恢复状态，请保留备份。");
  std::vector<fs::path> roots{home};
  for (auto &value : manifest.at("contexts")) roots.push_back(fs::u8path(value.get<std::string>()));
  std::vector<Edit> edits; size_t i = 0;
  for (auto &entry : manifest.at("files")) {
    auto path = fs::u8path(entry.get<std::string>());
    require(path.is_absolute() && !same(path, home / "config.toml") &&
              (projectPathAllowed(path, roots) || profilePathAllowed(path, home)), "恢复记录含未知配置来源，未修改。");
    edits.push_back({path, read(folder / (std::to_string(i) + "-before.toml")), read(folder / (std::to_string(i) + "-after.toml"))}); ++i;
  }
  restoreEdits(edits); manifest["status"] = "rolled_back"; write(folder / "manifest.json", manifest.dump(2)); fs::remove(pending);
}
}
struct YilaiConfigSources {
  fs::path home, runtime, folder;
  std::vector<fs::path> roots;
  std::vector<Edit> edits;
  Json manifest;
  Json origins = Json::array();
  bool applied = false, verified = false;
};
extern "C" YilaiConfigSources *yilai_sources_prepare(const char *homeText, const char *runtimeText, char **error) {
  if (error) *error = nullptr;
  try {
    require(homeText && *homeText, "缺少配置目录。");
    auto result = std::make_unique<YilaiConfigSources>();
    result->home = fs::weakly_canonical(fs::absolute(fs::u8path(homeText)));
    recover(result->home); result->roots = contexts(result->home);
    result->runtime = runtimeText && *runtimeText ? fs::u8path(runtimeText) : yilai_sources::locate_runtime();
    require(!result->runtime.empty(), "未找到 Codex 运行时，请先安装并运行 Codex 后重试。");
    for (auto &cwd : result->roots) {
      auto loaded = probe(result->runtime, result->home, cwd);
      if (loaded.contains("origins") && loaded["origins"].is_object()) {
        auto id = loaded["config"].contains("model_provider") && loaded["config"]["model_provider"].is_string() ? loaded["config"]["model_provider"].get<std::string>() : std::string("openai");
        for (const auto &field : std::vector<std::string>{"model_provider", "features.image_generation", "forced_login_method", "openai_base_url", "model_providers." + id + ".base_url", "model_providers." + id + ".experimental_bearer_token", "model_providers." + id + ".requires_openai_auth"}) {
          if (loaded["origins"].contains(field) && loaded["origins"][field].contains("name"))
            result->origins.push_back({{"cwd", text(cwd)}, {"field", field}, {"source", loaded["origins"][field]["name"]}});
        }
      }
      bool sawUser = false;
      for (auto &layer : loaded["layers"]) {
        require(layer.is_object() && layer.contains("name") && layer["name"].is_object() && layer["name"].contains("type") && layer["name"]["type"].is_string(), "Codex 配置来源结构不受支持，未修改原文件。");
        require(!layer.contains("disabledReason") || layer["disabledReason"].is_null() || layer["disabledReason"].is_string(), "Codex 配置来源启用状态不受支持。");
        if (layer.contains("disabledReason") && layer["disabledReason"].is_string() && !layer["disabledReason"].get<std::string>().empty()) continue;
        require(layer.contains("config") && layer["config"].is_object(), "Codex 未返回完整的配置来源内容，未修改原文件。");
        const auto &name = layer.at("name"); auto type = name.value("type", "");
        fs::path path;
        if (name.contains("file") && name["file"].is_string()) path = fs::u8path(name["file"].get<std::string>());
        if (type == "user" && !path.empty() && same(path, result->home / "config.toml")) { sawUser = true; continue; }
        if (sawUser) continue;
        if (type == "project" && name.contains("dotCodexFolder")) path = fs::u8path(name.at("dotCodexFolder").get<std::string>()) / "config.toml";
        bool allowed = (type == "project" && projectPathAllowed(path, result->roots)) ||
                       ((type == "user" || type == "profile") && name.contains("profile") && name["profile"].is_string() && !name["profile"].get<std::string>().empty() && !path.empty() && profilePathAllowed(path, result->home));
        // Layers with no connection fields cannot block this operation.
        auto fields = layer.value("config", Json::object());
        bool relevant = fields.contains("model_provider") || fields.contains("forced_login_method") || fields.contains("forced_chatgpt_workspace_id") || fields.contains("openai_base_url") || fields.contains("chatgpt_base_url") ||
          (fields.contains("features") && fields["features"].is_object() && fields["features"].contains("image_generation")) ||
          (fields.contains("model_providers") && fields["model_providers"].is_object() && fields["model_providers"].contains("custom"));
        if (!relevant) continue;
        require(allowed && path.is_absolute(), "连接被不可自动修改的配置来源覆盖（" + type + "），请先移除对应启动覆盖或联系管理员。");
        if (std::any_of(result->edits.begin(), result->edits.end(), [&](auto &e) { return same(e.path, path); })) continue;
        auto before = read(path); char *detail = nullptr;
        std::unique_ptr<char, decltype(&yilai_config_free)> after(yilai_clear_connection_overrides(before.c_str(), &detail), yilai_config_free), errorText(detail, yilai_config_free);
        require(after != nullptr, errorText ? errorText.get() : "无法处理配置来源。");
        if (toml::parse(before) != toml::parse(after.get())) result->edits.push_back({path, before, after.get()});
      }
      require(sawUser, "无法确认用户配置所在的优先级，未修改任何来源。");
    }
    return result.release();
  } catch (const toml::parse_error &) { failure(error, "配置来源 TOML 无法解析，未修改原文件。"); }
  catch (const Json::exception &) { failure(error, "配置来源格式不受支持，未继续修改。"); }
  catch (const std::exception &e) { failure(error, e.what()); }
  catch (...) { failure(error, "配置来源识别失败。"); }
  return nullptr;
}
extern "C" char *yilai_sources_summary(const YilaiConfigSources *c) {
  if (!c) return copy("配置来源未启用。");
  Json report{{"contexts", c->roots.size()}, {"override_files", c->edits.size()}, {"paths", Json::array()}, {"effective_origins", c->origins}};
  for (auto &e : c->edits) report["paths"].push_back(text(e.path));
  return copy(report.dump());
}
extern "C" int yilai_sources_apply(YilaiConfigSources *c, char **error) {
  if (error) *error = nullptr;
  try {
    require(c != nullptr, "缺少来源计划。");
    if (c->edits.empty()) return 1;
    auto parent = c->home / "yilai-source-backups"; privateDirectory(parent);
    c->folder = parent / unique(); privateDirectory(c->folder);
    c->manifest = {{"version", 1}, {"home", text(c->home)}, {"status", "pending"}, {"contexts", Json::array()}, {"files", Json::array()}};
    for (auto &root : c->roots) c->manifest["contexts"].push_back(text(root));
    for (size_t i = 0; i < c->edits.size(); ++i) {
      auto &edit = c->edits[i]; require(read(edit.path) == edit.before, "配置来源已发生变化，请重试：" + text(edit.path));
      write(c->folder / (std::to_string(i) + "-before.toml"), edit.before);
      write(c->folder / (std::to_string(i) + "-after.toml"), edit.after);
      c->manifest["files"].push_back(text(edit.path));
    }
    write(c->folder / "manifest.json", c->manifest.dump(2));
    write(parent / "pending.json", Json{{"generation", c->folder.filename().string()}}.dump());
    c->applied = true;
    for (auto &edit : c->edits) { require(read(edit.path) == edit.before, "配置来源被外部修改，请重试：" + text(edit.path)); write(edit.path, edit.after); }
    return 1;
  } catch (const std::exception &e) { failure(error, e.what()); } catch (...) { failure(error, "配置来源清理失败。"); }
  return 0;
}
extern "C" int yilai_sources_verify(YilaiConfigSources *c, const char *key, char **error) {
  if (error) *error = nullptr;
  try {
    require(c != nullptr, "缺少来源计划。");
    for (auto &cwd : c->roots) {
      auto loaded = probe(c->runtime, c->home, cwd); const auto &config = loaded.at("config");
      require(config.value("model_provider", "openai") == "custom", "连接选择仍被覆盖，未确认切换成功：" + text(cwd));
      const auto &provider = config.at("model_providers").at("custom");
      require(!provider.contains("env_key") || provider["env_key"].is_null() || provider["env_key"] == "", "认证仍受其他来源的环境变量配置影响。");
      require(!provider.value("requires_openai_auth", false), "认证方式仍被其他来源覆盖：" + text(cwd));
      require(provider.value("base_url", "") == "https://api.yilai-ai.com" && provider.value("experimental_bearer_token", "") == (key ? key : ""), "地址或认证凭据未按本次配置生效。");
      require(config.at("features").value("image_generation", false), "生图开关仍被其他来源覆盖。");
      require(provider.at("http_headers").value("x-openai-actor-authorization", "") == "local-image-extension", "生图连接设置未生效。");
      for (const char *field : {"forced_login_method", "forced_chatgpt_workspace_id", "openai_base_url", "chatgpt_base_url"})
        require(!loaded.value("origins", Json::object()).contains(field) || !config.contains(field) || config[field].is_null() || config[field] == "", std::string("连接仍受覆盖字段影响：") + field);
    }
    c->verified = true; return 1;
  } catch (const Json::exception &) { failure(error, "无法从 Codex 返回值确认新连接，切换未完成。"); }
  catch (const std::exception &e) { failure(error, e.what()); } catch (...) { failure(error, "配置生效核验失败。"); }
  return 0;
}
extern "C" int yilai_sources_rollback(YilaiConfigSources *c, char **error) {
  if (error) *error = nullptr;
  try {
    if (!c || !c->applied) return 1;
    c->verified = false;
    restoreEdits(c->edits); c->manifest["status"] = "rolled_back"; write(c->folder / "manifest.json", c->manifest.dump(2));
    fs::remove(c->home / "yilai-source-backups" / "pending.json"); c->applied = false; return 1;
  } catch (const std::exception &e) {
    if (c && !c->folder.empty()) try { c->manifest["status"] = "recovery_required"; write(c->folder / "manifest.json", c->manifest.dump(2)); } catch (...) {}
    failure(error, e.what());
  } catch (...) { failure(error, "配置来源恢复失败，请保留备份。"); }
  return 0;
}
extern "C" void yilai_sources_finish(YilaiConfigSources *c) {
  // API configuration is committed independently of optional history sync.
  // Verification alone is not a commit while configuration rollback is possible.
  if (c && c->verified && c->applied) {
    try {
      c->manifest["status"] = "complete";
      write(c->folder / "manifest.json", c->manifest.dump(2));
      std::error_code ignored;
      fs::remove(c->home / "yilai-source-backups" / "pending.json", ignored);
    } catch (...) { /* Leave pending backups for recovery on the next switch. */ }
  }
  delete c;
}
