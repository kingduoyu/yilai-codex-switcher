#include "../ConfigRewrite/vendor/toml.hpp"
#include "ConfigRewrite.h"
#include "HistoryRepair.h"
#include "../Shared/vendor/json.hpp"
#include "vendor/sqlite3.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <map>
#include <vector>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace {
void ensure(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
std::string utf8(const fs::path &p) {
  auto s = p.u8string();
  return {s.begin(), s.end()};
}
fs::path from(const std::string &s) { return fs::u8path(s); }
void noLinks(const fs::path &p) {
  auto current = fs::absolute(p).lexically_normal();
  while (!current.empty()) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesW(current.c_str());
    ensure(attrs == INVALID_FILE_ATTRIBUTES ||
               !(attrs & FILE_ATTRIBUTE_REPARSE_POINT),
           "History path contains a reparse point: " + utf8(current));
#else
    ensure(!fs::is_symlink(fs::symlink_status(current)),
           "History path contains a symbolic link: " + utf8(current));
#endif
    auto parent = current.parent_path();
    if (parent == current)
      break;
    current = parent;
  }
}
std::string read(const fs::path &p) {
  noLinks(p);
  ensure(fs::is_regular_file(p) && fs::file_size(p) <= 512ULL * 1024 * 1024,
         "Invalid or oversized history file: " + utf8(p));
  std::ifstream f(p, std::ios::binary);
  ensure(bool(f), "Cannot read: " + utf8(p));
  std::ostringstream s;
  s << f.rdbuf();
  ensure(!f.bad(), "Read failed: " + utf8(p));
  return s.str();
}
void write(const fs::path &p, const std::string &bytes) {
  noLinks(p);
  fs::create_directories(p.parent_path());
  auto tmp = p;
  tmp +=
      ".yilai-tmp-" +
      std::to_string(
          std::chrono::high_resolution_clock::now().time_since_epoch().count());
#ifdef _WIN32
  HANDLE handle =
      CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  ensure(handle != INVALID_HANDLE_VALUE, "Cannot create temporary file");
  DWORD written = 0;
  bool ok = bytes.size() <= MAXDWORD &&
            WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()),
                      &written, nullptr) &&
            written == bytes.size() && FlushFileBuffers(handle);
  CloseHandle(handle);
  if (!ok || !MoveFileExW(tmp.c_str(), p.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::error_code ignored;
    fs::remove(tmp, ignored);
    throw std::runtime_error("Atomic write failed: " + utf8(p));
  }
#else
  int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  ensure(fd >= 0, "Cannot create temporary file");
  size_t done = 0;
  bool ok = true;
  while (done < bytes.size()) {
    auto n = ::write(fd, bytes.data() + done, bytes.size() - done);
    if (n <= 0) {
      ok = false;
      break;
    }
    done += size_t(n);
  }
  ok = (::fsync(fd) == 0) && ok;
  ::close(fd);
  if (!ok || ::rename(tmp.c_str(), p.c_str()) != 0) {
    std::error_code ignored;
    fs::remove(tmp, ignored);
    throw std::runtime_error("Atomic write failed: " + utf8(p));
  }
#endif
}
struct Db {
  sqlite3 *p = nullptr;
  bool transaction = false;
  Db(const fs::path &path, bool create = false) {
    noLinks(path);
    int rc = sqlite3_open_v2(
        utf8(path).c_str(), &p,
        SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0), nullptr);
    if (rc != SQLITE_OK) {
      if (p)
        sqlite3_close(p);
      p = nullptr;
      throw std::runtime_error("Cannot open history database: " + utf8(path));
    }
    sqlite3_busy_timeout(p, 1500);
  }
  ~Db() {
    if (p) {
      if (transaction)
        sqlite3_exec(p, "ROLLBACK", nullptr, nullptr, nullptr);
      sqlite3_close(p);
    }
  }
  void exec(const char *sql) {
    ensure(sqlite3_exec(p, sql, nullptr, nullptr, nullptr) == SQLITE_OK,
           "SQLite operation failed (close Codex and CCS, then retry)");
  }
  void begin() {
    exec("BEGIN IMMEDIATE");
    transaction = true;
  }
  void commit() {
    exec("COMMIT");
    transaction = false;
  }
  void rollback() {
    if (transaction) {
      exec("ROLLBACK");
      transaction = false;
    }
  }
};
struct Stmt {
  sqlite3_stmt *p = nullptr;
  Stmt(Db &db, const char *sql) {
    ensure(sqlite3_prepare_v2(db.p, sql, -1, &p, nullptr) == SQLITE_OK,
           "Unsupported history database schema");
  }
  ~Stmt() {
    if (p)
      sqlite3_finalize(p);
  }
  void bind(int i, const Json &value) {
    int rc = value.is_null()
                 ? sqlite3_bind_null(p, i)
                 : sqlite3_bind_text(
                       p, i, value.get_ref<const std::string &>().c_str(), -1,
                       SQLITE_TRANSIENT);
    ensure(rc == SQLITE_OK, "SQLite bind failed");
  }
};
struct Meta {
  size_t offset = 0, length = 0;
  std::string raw, id;
  Json record;
  bool bom = false, cr = false;
};
Meta metadata(const std::string &bytes, const fs::path &path = {},
              bool validateTail = true) try {
  Meta result;
  bool found = false;
  size_t start = 0;
  while (start < bytes.size()) {
    auto end = bytes.find('\n', start);
    if (end == std::string::npos)
      end = bytes.size();
    auto line = bytes.substr(start, end - start);
    bool bom = start == 0 && line.rfind("\xEF\xBB\xBF", 0) == 0;
    std::string parse = bom ? line.substr(3) : line;
    bool cr = !parse.empty() && parse.back() == '\r';
    if (cr)
      parse.pop_back();
    if (!parse.empty()) {
      // Parse without exception snippets: history text must not enter logs.
      auto value = Json::parse(parse, nullptr, false);
      ensure(!value.is_discarded() && value.is_object(), "Invalid JSONL record");
      ensure(!value.contains("type") || value["type"].is_string(),
             "Invalid JSONL record type");
      if (value.value("type", std::string()) == "session_meta") {
        ensure(value.contains("payload") && value["payload"].is_object(),
               "Missing session metadata payload");
        auto &payload = value["payload"];
        ensure(payload.contains("id") && payload["id"].is_string(),
               "Invalid session ID");
        ensure(!payload.contains("model_provider") ||
                   payload["model_provider"].is_string(),
               "Invalid session provider");
        // Codex uses the FIRST SessionMeta as this rollout's identity.
        // Later records can be inherited fork history with different IDs and
        // providers; validate them but preserve their original bytes.
        if (!found) {
          result = {start, end - start, line, payload["id"].get<std::string>(),
                    value, bom,         cr};
          found = true;
          if (!validateTail)
            break;
        }
      }
    }
    start = end + 1;
  }
  ensure(found, "Session metadata not found");
  return result;
} catch (const std::exception &e) {
  std::string location = path.empty() ? "" : " [" + utf8(path) + "]";
  for (char &c : location)
    if (static_cast<unsigned char>(c) < 32)
      c = '?';
  throw std::runtime_error("Invalid history metadata" + location + ": " + e.what());
}
Meta quickMetadata(const fs::path &path) {
  noLinks(path);
  ensure(fs::is_regular_file(path) &&
             fs::file_size(path) <= 512ULL * 1024 * 1024,
         "Invalid or oversized history file: " + utf8(path));
  constexpr size_t prefixLimit = 64 * 1024;
  const auto size = fs::file_size(path);
  if (size <= prefixLimit)
    return metadata(read(path), path, false);
  std::ifstream input(path, std::ios::binary);
  ensure(bool(input), "Cannot read: " + utf8(path));
  std::string prefix(prefixLimit, '\0');
  input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
  ensure(!input.bad(), "Read failed: " + utf8(path));
  prefix.resize(static_cast<size_t>(input.gcount()));
  try {
    return metadata(prefix, path, false);
  } catch (...) {
    // Preserve compatibility with unusual files whose first SessionMeta is
    // beyond the prefix. Malformed candidates still fail during full checks.
    return metadata(read(path), path, false);
  }
}
std::string changedLine(const Meta &m, const Json &provider) {
  auto value = m.record;
  if (provider.is_null())
    value["payload"].erase("model_provider");
  else
    value["payload"]["model_provider"] = provider;
  return (m.bom ? "\xEF\xBB\xBF" : "") + value.dump() + (m.cr ? "\r" : "");
}
Json currentProvider(const Meta &m) {
  return m.record["payload"].contains("model_provider")
             ? m.record["payload"]["model_provider"]
             : Json(nullptr);
}

struct Report {
  size_t files = 0, rows = 0, unchanged = 0, skipped = 0;
  std::vector<std::string> issues;
  void skip(const fs::path &path, const std::string &reason) {
    ++skipped;
    if (issues.size() < 3) {
      auto name = utf8(path.filename());
      for (auto &c : name) if (static_cast<unsigned char>(c) < 32) c = '?';
      if (name.size() > 160) name = "history";
      issues.push_back(name + ": " + reason);
    }
  }
  Json json() const {
    std::string message = "修复结束：对话文件 " + std::to_string(files) +
        "，索引 " + std::to_string(rows) + "，跳过/异常 " + std::to_string(skipped) + " 项。";
    if (!files && !rows && !skipped) message = "没有需要修复的旧易来对话。";
    if (skipped) message += " 已完成的修改保留，可处理提示后重试。";
    for (const auto &issue : issues) message += "\n" + issue;
    message += "\n连接配置与登录未修改。";
    return {{"files", files}, {"rows", rows}, {"unchanged", unchanged},
            {"skipped", skipped}, {"warning", skipped != 0}, {"message", message}};
  }
};
std::vector<fs::path> sessionFiles(const fs::path &home, Report &report) {
  std::vector<fs::path> out;
  std::vector<std::pair<fs::path, unsigned>> pending;
  for (const auto *name : {"sessions", "archived_sessions"}) pending.push_back({home / name, 0});
  while (!pending.empty()) {
    auto entry = pending.back();
    pending.pop_back();
    try {
      noLinks(entry.first);
      if (!fs::exists(entry.first)) continue;
      ensure(entry.second < 32, "目录层级过深，已跳过");
      std::error_code ec;
      fs::directory_iterator it(entry.first, ec), end;
      ensure(!ec, "目录无法读取");
      while (it != end) {
        auto path = it->path();
        try {
          noLinks(path);
          if (it->is_directory()) pending.push_back({path, entry.second + 1});
          else if (path.extension() == ".jsonl" && it->is_regular_file()) out.push_back(path);
        } catch (...) { report.skip(path, "路径不可访问或为链接，未修改"); }
        it.increment(ec);
        if (ec) { report.skip(entry.first, "目录枚举未完成"); break; }
      }
    } catch (...) { report.skip(entry.first, "目录不可访问或为链接，未修改"); }
  }
  std::sort(out.begin(), out.end());
  return out;
}
struct Row { std::string id, provider, path; };
struct Index {
  fs::path path;
  std::unique_ptr<Db> db;
  std::vector<Row> entries;
  bool hasPath = false;
};
std::string column(sqlite3_stmt *q, int i) {
  ensure(sqlite3_column_type(q, i) == SQLITE_TEXT, "索引字段格式不支持");
  return std::string(reinterpret_cast<const char *>(sqlite3_column_text(q, i)), sqlite3_column_bytes(q, i));
}
std::string environment(const char *name) {
#ifdef _WIN32
  auto wname = fs::u8path(name).wstring();
  wchar_t *value = nullptr;
  size_t size = 0;
  _wdupenv_s(&value, &size, wname.c_str());
  auto result = value ? utf8(fs::path(value)) : std::string();
  free(value);
  return result;
#else
  const auto *value = std::getenv(name);
  return value ? value : "";
#endif
}
std::vector<fs::path> databaseDirs(const fs::path &home, const toml::table &config) {
  auto extra = config["sqlite_home"].value_or(std::string());
  if (extra.empty()) extra = environment("CODEX_SQLITE_HOME");
  std::vector<fs::path> dirs{home};
  if (!extra.empty()) {
    if (extra == "~" || extra.rfind("~/", 0) == 0 || extra.rfind("~\\", 0) == 0) {
#ifdef _WIN32
      auto user = environment("USERPROFILE");
#else
      auto user = environment("HOME");
#endif
      ensure(!user.empty(), "无法解析 sqlite_home");
      extra = utf8(extra == "~" ? from(user) : from(user) / from(extra.substr(2)));
    }
    auto dir = from(extra);
    ensure(dir.is_absolute(), "sqlite_home 必须为绝对路径，未执行修复");
    dir = fs::weakly_canonical(dir);
    if (dir != home) dirs.push_back(dir);
  }
  return dirs;
}
std::vector<Index> indexes(const std::vector<fs::path> &dirs, Report &report) {
  std::vector<Index> result;
  for (const auto &dir : dirs) {
    try {
      noLinks(dir);
      if (!fs::exists(dir)) continue;
      for (const auto &entry : fs::directory_iterator(dir)) {
        auto path = entry.path();
        auto name = utf8(path.filename());
        if (name.rfind("state_", 0) != 0 || path.extension() != ".sqlite") continue;
        try {
          ensure(name == "state_5.sqlite", "不支持的索引版本");
          noLinks(path);
          noLinks(from(utf8(path) + "-wal"));
          noLinks(from(utf8(path) + "-shm"));
          Index index;
          index.path = path;
          index.db = std::make_unique<Db>(path);
          index.db->exec("PRAGMA trusted_schema=OFF");
          {
            Stmt schema(*index.db, "PRAGMA table_info(threads)");
            int rc;
            while ((rc = sqlite3_step(schema.p)) == SQLITE_ROW)
              if (column(schema.p, 1) == "rollout_path") index.hasPath = true;
            ensure(rc == SQLITE_DONE, "索引结构读取失败");
          }
          Stmt query(*index.db, index.hasPath ? "SELECT id, model_provider, rollout_path FROM threads" : "SELECT id, model_provider FROM threads");
          int rc;
          while ((rc = sqlite3_step(query.p)) == SQLITE_ROW) {
            Row row{column(query.p, 0), sqlite3_column_type(query.p, 1) == SQLITE_NULL ? "" : column(query.p, 1), ""};
            if (index.hasPath && sqlite3_column_type(query.p, 2) != SQLITE_NULL) row.path = column(query.p, 2);
            index.entries.push_back(std::move(row));
          }
          ensure(rc == SQLITE_DONE, "索引读取失败");
          result.push_back(std::move(index));
        } catch (...) { report.skip(path, "索引被占用、损坏或版本不支持；其他可修复项继续处理"); }
      }
    } catch (...) { report.skip(dir, "索引目录不可读取"); }
  }
  return result;
}
struct Session { fs::path path; Meta meta; };
Json repair(const fs::path &input) {
  auto home = fs::canonical(input);
  const auto configBytes = read(home / "config.toml");
  toml::table config;
  try { config = toml::parse(configBytes); }
  catch (...) { throw std::runtime_error("配置格式错误，请先完成 API 配置；历史未修改"); }
  auto provider = config["model_provider"].value_or(std::string());
  auto profile = config["profile"].value_or(std::string());
  if (!profile.empty()) provider = config["profiles"][profile]["model_provider"].value_or(provider);
  ensure(provider == "custom" && yilai_config_mode(configBytes.c_str()) == 1,
         "请先配置易来 API，再手动修复旧易来对话；历史未修改");
  auto dirs = databaseDirs(home, config);
  Report report;
  std::map<std::string, std::vector<Session>> sessions;
  for (const auto &path : sessionFiles(home, report)) {
    try {
      auto meta = quickMetadata(path);
      ensure(!meta.id.empty(), "空会话 ID");
      sessions[meta.id].push_back({path, std::move(meta)});
    } catch (...) { report.skip(path, "对话元数据无法读取或格式错误，未修改"); }
  }
  auto dbs = indexes(dirs, report);
  std::map<std::string, std::vector<std::pair<size_t, size_t>>> rows;
  for (size_t i = 0; i < dbs.size(); ++i)
    for (size_t j = 0; j < dbs[i].entries.size(); ++j) rows[dbs[i].entries[j].id].push_back({i, j});
  std::set<std::string> ready;
  for (const auto &group : sessions) {
    const auto &id = group.first;
    const auto &files = group.second;
    if (files.size() != 1) {
      for (const auto &file : files) report.skip(file.path, "重复会话 ID，未修改");
      continue;
    }
    const auto &session = files.front();
    auto providerValue = currentProvider(session.meta);
    if (providerValue != "yilai" && providerValue != "custom") { ++report.unchanged; continue; }
    bool rowNeedsRepair = false, conflict = false;
    std::set<size_t> seen;
    for (const auto &location : rows[id]) {
      const auto &row = dbs[location.first].entries[location.second];
      rowNeedsRepair |= row.provider == "yilai";
      conflict |= !seen.insert(location.first).second || (row.provider != "yilai" && row.provider != "custom");
      if (!row.path.empty()) {
        std::error_code ec;
        auto match = fs::equivalent(from(row.path), session.path, ec);
        conflict |= ec || !match;
      }
    }
    if (conflict) { report.skip(session.path, "会话索引的归属或文件路径冲突，未修改"); continue; }
    if (providerValue == "custom" && !rowNeedsRepair) { ++report.unchanged; continue; }
    try {
      auto bytes = read(session.path);
      auto meta = metadata(bytes);
      ensure(meta.id == id && meta.raw == session.meta.raw, "会话扫描后已改变");
      ensure(read(home / "config.toml") == configBytes, "连接配置已被其他程序改动");
      if (providerValue == "yilai") {
        auto updated = bytes;
        updated.replace(meta.offset, meta.length, changedLine(meta, "custom"));
        ensure(read(session.path) == bytes, "对话已被其他程序改动");
        write(session.path, updated);
        ++report.files;
      }
      ready.insert(id);
    } catch (...) { report.skip(session.path, "对话内容损坏、文件被占用或已被改动，未完成修复"); }
  }
  // Commit each row independently. A failed index update never reverts a repaired
  // file; the next manual run recognizes custom-file/yilai-index partial work.
  for (auto &index : dbs) {
    for (const auto &row : index.entries) {
      if (row.provider != "yilai") continue;
      if (!ready.count(row.id)) {
        report.skip(index.path, "存在未匹配到可修复文件的旧索引，未修改该项");
        continue;
      }
      try {
        const auto &session = sessions.at(row.id).front();
        auto meta = quickMetadata(session.path);
        ensure(meta.id == row.id && currentProvider(meta) == "custom", "对话已改变");
        ensure(read(home / "config.toml") == configBytes, "连接配置已改变");
        index.db->begin();
        Stmt update(*index.db, index.hasPath
            ? "UPDATE threads SET model_provider='custom' WHERE id=? AND model_provider='yilai' AND rollout_path IS ?"
            : "UPDATE threads SET model_provider='custom' WHERE id=? AND model_provider='yilai'");
        update.bind(1, row.id);
        if (index.hasPath) update.bind(2, row.path.empty() ? Json(nullptr) : Json(row.path));
        ensure(sqlite3_step(update.p) == SQLITE_DONE && sqlite3_changes(index.db->p) == 1, "索引更新失败");
        index.db->commit();
        ++report.rows;
      } catch (...) {
        try { index.db->rollback(); }
        catch (...) { report.skip(index.path, "索引事务未能结束，已停止处理该索引"); break; }
        report.skip(index.path, "部分索引未更新；已修复文件保留，可关闭占用程序后重试");
      }
    }
  }
  return report.json();
}
char *copy(const std::string &text) {
  auto *result = static_cast<char *>(std::malloc(text.size() + 1));
  if (!result) throw std::bad_alloc();
  std::memcpy(result, text.c_str(), text.size() + 1);
  return result;
}

void selfTestRepair(const fs::path &home) {
  fs::create_directories(home / "sessions");
  fs::create_directories(home / "archived_sessions");
  char *error = nullptr;
  std::unique_ptr<char, decltype(&yilai_config_free)> config(yilai_configure_api("", "sk-synthetic-history-test", &error), yilai_config_free);
  std::unique_ptr<char, decltype(&yilai_config_free)> detail(error, yilai_config_free);
  ensure(config != nullptr, "Test config failed");
  auto configBytes = "sqlite_home = " + Json(utf8(home)).dump() + "\n" + config.get();
  write(home / "config.toml", configBytes);
  write(home / "auth.json", "untouched-auth");
  write(home / "yilai-history-backups/pending.json", "untouched-old-marker");
  auto line = [](const std::string &id, const std::string &provider) {
    return Json({{"type", "session_meta"}, {"payload", {{"id", id}, {"model_provider", provider}}}}).dump();
  };
  auto tail = "\r\n" + line("inherited", "yilai") + "\r\n{\"type\":\"event_msg\",\"payload\":{\"message\":\"untouched\"}}\r\n";
  auto good = "\xEF\xBB\xBF" + line("good", "yilai") + tail;
  write(home / "sessions/good.jsonl", good);
  write(home / "archived_sessions/archive.jsonl", line("archive", "yilai") + "\n");
  write(home / "sessions/resume.jsonl", line("resume", "custom") + "\n");
  write(home / "sessions/fail.jsonl", line("fail", "yilai") + "\n");
  write(home / "sessions/conflict.jsonl", line("conflict", "yilai") + "\n");
  auto duplicate = line("duplicate", "yilai") + "\n";
  write(home / "sessions/duplicate-a.jsonl", duplicate);
  write(home / "sessions/duplicate-b.jsonl", duplicate);
  auto malformed = line("malformed", "yilai") + "\ninvalid-tail\n";
  write(home / "sessions/malformed.jsonl", malformed);
  write(home / "sessions/broken.jsonl", "invalid-json\n");
  auto official = line("official", "openai") + "\n";
  write(home / "sessions/official.jsonl", official);
  {
    Db db(home / "state_5.sqlite", true);
    db.exec("CREATE TABLE threads (id TEXT PRIMARY KEY, model_provider TEXT, rollout_path TEXT, title TEXT)");
    for (const auto &entry : std::vector<std::pair<std::string, std::string>>{
             {"good", "sessions/good.jsonl"}, {"archive", "archived_sessions/archive.jsonl"},
             {"resume", "sessions/resume.jsonl"}, {"fail", "sessions/fail.jsonl"},
             {"conflict", "sessions/good.jsonl"}, {"duplicate", "sessions/duplicate-a.jsonl"},
             {"malformed", "sessions/malformed.jsonl"}, {"orphan", "sessions/missing.jsonl"}}) {
      Stmt insert(db, "INSERT INTO threads VALUES (?, 'yilai', ?, 'keep-title')");
      insert.bind(1, entry.first);
      insert.bind(2, utf8(home / entry.second));
      ensure(sqlite3_step(insert.p) == SQLITE_DONE, "Test insert failed");
    }
    db.exec("CREATE TRIGGER reject_one BEFORE UPDATE ON threads WHEN OLD.id='fail' BEGIN SELECT RAISE(ABORT, 'synthetic'); END");
  }
  auto first = repair(home);
  ensure(first["files"] == 3 && first["rows"] == 3 && first["warning"] == true, "Repair counts or partial-success reporting incorrect");
  auto updated = read(home / "sessions/good.jsonl");
  ensure(updated.substr(updated.find('\n')) == good.substr(good.find('\n')) && updated.rfind("\xEF\xBB\xBF", 0) == 0,
         "Repair changed inherited metadata, BOM or conversation body");
  ensure(currentProvider(metadata(updated)) == "custom", "Provider was not repaired");
  ensure(read(home / "sessions/duplicate-a.jsonl") == duplicate && read(home / "sessions/duplicate-b.jsonl") == duplicate,
         "Duplicate sessions modified");
  ensure(read(home / "sessions/malformed.jsonl") == malformed && read(home / "sessions/official.jsonl") == official,
         "Malformed or unrelated session modified");
  ensure(currentProvider(metadata(read(home / "sessions/conflict.jsonl"))) == "yilai", "Conflicting path modified");
  ensure(currentProvider(metadata(read(home / "sessions/fail.jsonl"))) == "custom", "Successful file repair was rolled back after index failure");
  {
    Db db(home / "state_5.sqlite");
    Stmt query(db, "SELECT model_provider,title FROM threads WHERE id='fail'");
    ensure(sqlite3_step(query.p) == SQLITE_ROW && column(query.p, 0) == "yilai" && column(query.p, 1) == "keep-title", "Failed row or title was changed");
    db.exec("DROP TRIGGER reject_one");
  }
  auto resumed = repair(home);
  ensure(resumed["files"] == 0 && resumed["rows"] == 1, "Retry did not repair only the unfinished index row");
  auto repeat = repair(home);
  ensure(repeat["files"] == 0 && repeat["rows"] == 0, "Manual repair is not idempotent");
  ensure(read(home / "config.toml") == configBytes && read(home / "auth.json") == "untouched-auth" &&
         read(home / "yilai-history-backups/pending.json") == "untouched-old-marker", "Repair changed config, auth or old backups");
  write(home / "config.toml", "model_provider = 'openai'\n");
  bool rejected = false;
  try { repair(home); } catch (...) { rejected = true; }
  ensure(rejected, "Repair accepted official mode");
}
} // namespace
extern "C" char *yilai_repair_history(const char *home, char **error) {
  if (error) *error = nullptr;
  try { return copy(repair(from(home ? home : "")).dump()); }
  catch (const std::exception &e) { if (error) *error = copy(e.what()); return nullptr; }
}
extern "C" int yilai_history_repair_self_test(char **error) {
  if (error) *error = nullptr;
  auto home = fs::canonical(fs::temp_directory_path()) /
      ("yilai-repair-test-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
  try {
    ensure(fs::create_directory(home), "Cannot create isolated repair fixture");
    selfTestRepair(home);
    fs::remove_all(home);
    return 1;
  } catch (const std::exception &e) {
    if (error) *error = copy(std::string(e.what()) + " [fixture: " + utf8(home) + "]");
    return 0;
  }
}
