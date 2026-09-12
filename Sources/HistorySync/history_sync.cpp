#include "../ConfigRewrite/vendor/toml.hpp"
#include "ConfigRewrite.h"
#include "HistorySync.h"
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
struct Lock {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
#else
  int handle = -1;
#endif
  explicit Lock(const fs::path &home) {
    noLinks(home);
    fs::create_directories(home);
    auto p = home / ".yilai-history.lock";
    noLinks(p);
#ifdef _WIN32
    handle = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    ensure(handle != INVALID_HANDLE_VALUE,
           "Another history operation is running");
#else
    handle = ::open(p.c_str(), O_RDWR | O_CREAT, 0600);
    if (handle < 0 || flock(handle, LOCK_EX | LOCK_NB) != 0) {
      if (handle >= 0)
        ::close(handle);
      throw std::runtime_error("Another history operation is running");
    }
#endif
  }
  ~Lock() {
#ifdef _WIN32
    if (handle != INVALID_HANDLE_VALUE)
      CloseHandle(handle);
#else
    if (handle >= 0) {
      flock(handle, LOCK_UN);
      ::close(handle);
    }
#endif
  }
};
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
Json rows(Db &db) {
  Stmt integrity(db, "PRAGMA quick_check");
  ensure(sqlite3_step(integrity.p) == SQLITE_ROW &&
             std::string(reinterpret_cast<const char *>(
                 sqlite3_column_text(integrity.p, 0))) == "ok",
         "History database integrity check failed");
  Stmt q(db, "SELECT id,model_provider FROM threads ORDER BY id");
  Json out = Json::array();
  int rc;
  while ((rc = sqlite3_step(q.p)) == SQLITE_ROW) {
    ensure(sqlite3_column_type(q.p, 0) == SQLITE_TEXT, "Invalid thread ID");
    ensure(sqlite3_column_type(q.p, 1) == SQLITE_TEXT ||
               sqlite3_column_type(q.p, 1) == SQLITE_NULL,
           "Invalid provider field");
    auto id = std::string(
        reinterpret_cast<const char *>(sqlite3_column_text(q.p, 0)));
    Json provider =
        sqlite3_column_type(q.p, 1) == SQLITE_NULL
            ? Json(nullptr)
            : Json(reinterpret_cast<const char *>(sqlite3_column_text(q.p, 1)));
    out.push_back({id, provider});
  }
  ensure(rc == SQLITE_DONE, "History database read failed");
  return out;
}
Json migratableRows(Db &db) {
  Stmt q(db, "SELECT id,model_provider FROM threads "
             "WHERE model_provider='yilai' ORDER BY id");
  Json out = Json::array();
  int rc;
  while ((rc = sqlite3_step(q.p)) == SQLITE_ROW) {
    ensure(sqlite3_column_type(q.p, 0) == SQLITE_TEXT,
           "Invalid thread ID");
    out.push_back(
        {std::string(reinterpret_cast<const char *>(sqlite3_column_text(q.p, 0))),
         "yilai"});
  }
  ensure(rc == SQLITE_DONE, "History database read failed");
  return out;
}
void backupDb(Db &db, const fs::path &target) {
  Db dest(target, true);
  sqlite3_backup *b = sqlite3_backup_init(dest.p, "main", db.p, "main");
  ensure(b != nullptr, "Database backup failed");
  int rc = sqlite3_backup_step(b, -1);
  int finish = sqlite3_backup_finish(b);
  ensure(rc == SQLITE_DONE && finish == SQLITE_OK && rows(db) == rows(dest),
         "Database backup verification failed");
}
void updateRows(Db &db, const Json &changes, bool reverse = false) {
  for (const auto &row : changes) {
    Stmt q(db, "UPDATE threads SET model_provider=? WHERE id=? AND "
               "model_provider IS ?");
    q.bind(1, row[reverse ? 1 : 2]);
    q.bind(2, row[0]);
    q.bind(3, row[reverse ? 2 : 1]);
    ensure(sqlite3_step(q.p) == SQLITE_DONE && sqlite3_changes(db.p) == 1,
           "History changed during synchronization; no concurrent writers are "
           "allowed");
  }
}
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
std::vector<fs::path> sessionFiles(const fs::path &home) {
  std::vector<fs::path> out;
  for (const char *name : {"sessions", "archived_sessions"}) {
    auto dir = home / name;
    noLinks(dir);
    if (!fs::exists(dir))
      continue;
    for (auto it = fs::recursive_directory_iterator(dir);
         it != fs::recursive_directory_iterator(); ++it) {
      noLinks(it->path());
      ensure(it.depth() < 16, "History directory is too deep");
      if (it->is_regular_file() && it->path().extension() == ".jsonl")
        out.push_back(it->path());
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}
std::vector<fs::path> databases(const fs::path &home,
                                const std::string &config) {
  auto doc = toml::parse(config);
  std::vector<fs::path> dirs{home};
  std::string extra = doc["sqlite_home"].value_or(std::string());
  if (extra.empty()) {
#ifdef _WIN32
    // The narrow CRT environment is encoded in the Windows code page,
    // not UTF-8. Preserve Unicode user/profile paths through the wide API.
    wchar_t *value = nullptr;
    size_t count = 0;
    _wdupenv_s(&value, &count, L"CODEX_SQLITE_HOME");
    if (value) {
      extra = utf8(fs::path(value));
      free(value);
    }
#else
    const char *value = std::getenv("CODEX_SQLITE_HOME");
    if (value)
      extra = value;
#endif
  }
  if (!extra.empty()) {
    if (extra == "~" || extra.rfind("~/", 0) == 0 ||
        extra.rfind("~\\", 0) == 0) {
#ifdef _WIN32
      wchar_t *value = nullptr;
      size_t n = 0;
      _wdupenv_s(&value, &n, L"USERPROFILE");
      ensure(value != nullptr, "Cannot expand sqlite_home");
      fs::path user(value);
      free(value);
#else
      const char *value = std::getenv("HOME");
      ensure(value != nullptr, "Cannot expand sqlite_home");
      fs::path user(value);
#endif
      extra = utf8(extra == "~" ? user : user / from(extra.substr(2)));
    }
    auto p = from(extra);
    ensure(p.is_absolute(), "sqlite_home must be an absolute path");
    p = fs::weakly_canonical(p).lexically_normal();
    if (p != home)
      dirs.push_back(p);
  }
  std::set<fs::path> out;
  for (const auto &dir : dirs) {
    noLinks(dir);
    if (!fs::exists(dir))
      continue;
    for (const auto &entry : fs::directory_iterator(dir)) {
      auto name = utf8(entry.path().filename());
      if (name.rfind("state_", 0) != 0 || entry.path().extension() != ".sqlite")
        continue;
      ensure(name == "state_5.sqlite",
             "Unsupported state database version; history left unchanged");
      noLinks(entry.path());
      out.insert(entry.path());
    }
  }
  return {out.begin(), out.end()};
}
// History unification never changes connection or login settings.
std::string normalize(const std::string &config) { return config; }
// The caller holds the home lock, including across recovery and a new sync.
Json operateLocked(const fs::path &home, bool restore, int failAfter = 0,
                   bool restoreConfig = true) {
  auto configPath = home / "config.toml";
  std::string oldConfig = fs::exists(configPath) ? read(configPath) : "";
  std::string newConfig = restore ? oldConfig : normalize(oldConfig);
  // Repair only the provider id written by our older configurator.
  auto canMigrate = [](const Json &provider) { return provider == "yilai"; };
  auto parent = home / "yilai-history-backups";
  noLinks(parent);
  Json source;
  auto pointer = parent / "latest.json";
  if (fs::exists(parent / "pending.json")) {
    auto pending = Json::parse(read(parent / "pending.json"));
    auto pendingName = pending.at("generation").get<std::string>();
    ensure(!pendingName.empty() && pendingName.find_first_not_of(
                                       "0123456789-") == std::string::npos,
           "Invalid pending backup pointer");
    auto pendingPlan =
        Json::parse(read(parent / pendingName / "manifest.json"));
    auto status = pendingPlan.value("status", std::string());
    if (status == "rolled_back" || status == "restored")
      fs::remove(parent / "pending.json");
    else {
      ensure(restore, "An interrupted synchronization needs recovery. Choose "
                      "Undo last sync first.");
      pointer = parent / "pending.json";
    }
  }
  if (restore && fs::exists(pointer)) {
    auto latest = Json::parse(read(pointer));
    auto generation = latest.at("generation").get<std::string>();
    ensure(!generation.empty() &&
               generation.find_first_not_of("0123456789-") == std::string::npos,
           "Invalid history backup pointer");
    source = Json::parse(read(parent / generation / "manifest.json"));
    ensure(source.at("home") == utf8(home),
           "History backup belongs to another directory");
    ensure(source.value("kind", std::string()) == "sync" &&
               source.value("status", std::string()) != "restored",
           "No synchronization to undo");
    if (restoreConfig && oldConfig == source.at("new_config"))
      newConfig = source.at("old_config");
  } else if (restore)
    throw std::runtime_error("No local history synchronization backup found");
  std::string generation = std::to_string(
      std::chrono::system_clock::now().time_since_epoch().count());
  auto backup = parent / generation;
  ensure(!fs::exists(backup), "Backup generation collision");
  Json plan = {{"version", 1},
               {"kind", restore ? "restore" : "sync"},
               {"home", utf8(home)},
               {"old_config", oldConfig},
               {"new_config", newConfig},
               {"status", "prepared"},
               {"files", Json::array()},
               {"databases", Json::array()}};
  std::set<std::string> seenIds;
  auto files = restore ? std::vector<fs::path>{} : sessionFiles(home);
  if (restore)
    for (const auto &record : source["files"])
      files.push_back(from(record.at("path")));
  for (const auto &file : files) {
    auto relative = file.lexically_relative(home);
    ensure(file.is_absolute() && file == file.lexically_normal() &&
               !relative.empty() &&
               (*relative.begin() == "sessions" ||
                *relative.begin() == "archived_sessions") &&
               file.extension() == ".jsonl",
           "Invalid history backup path");
    ensure(fs::exists(file),
           "A synchronized history file is missing; no changes were made");
    std::string bytes;
    auto meta = restore ? (bytes = read(file), metadata(bytes, file))
                        : quickMetadata(file);
    Json fromProvider = currentProvider(meta), toProvider = "custom";
    if (!restore) {
      ensure(seenIds.insert(meta.id).second,
             "Duplicate session IDs require manual review");
    }
    if (restore) {
      auto found = std::find_if(
          source["files"].begin(), source["files"].end(),
          [&](const Json &r) { return r.at("path") == utf8(file); });
      ensure(found != source["files"].end() && found->at("id") == meta.id,
             "Session identity changed");
      if (fromProvider != "custom")
        continue;
      toProvider = found->at("from");
    }
    if (fromProvider == toProvider || (!restore && !canMigrate(fromProvider)))
      continue;
    if (!restore) {
      // Validate the complete JSONL only for a file we are about to rewrite.
      // Already-custom and unrelated sessions take the fast prefix-only path.
      bytes = read(file);
      meta = metadata(bytes, file, true);
      fromProvider = currentProvider(meta);
    }
    plan["files"].push_back(
        {{"path", utf8(file)},
         {"id", meta.id},
         {"from", fromProvider},
         {"to", toProvider},
         {"old_line", meta.raw},
         {"new_line", changedLine(meta, toProvider)},
         {"offset", meta.offset},
         {"size", bytes.size()},
         {"backup", std::to_string(plan["files"].size()) + ".jsonl"}});
  }
  auto dbPaths = restore ? std::vector<fs::path>{} : databases(home, oldConfig);
  if (restore) {
    // A backup may only refer to databases in this Codex home's configured
    // SQLite locations. Do not treat arbitrary manifest paths as authority.
    auto allowed = databases(home, source.at("old_config").get<std::string>());
    auto current = databases(home, oldConfig);
    allowed.insert(allowed.end(), current.begin(), current.end());
    std::set<fs::path> unique;
    for (const auto &record : source["databases"]) {
      auto p = from(record.at("path"));
      ensure(p.is_absolute() && p == p.lexically_normal() &&
                 std::find(allowed.begin(), allowed.end(), p) !=
                     allowed.end() &&
                 unique.insert(p).second,
             "Invalid database backup path; restore the original sqlite_home "
             "setting before Undo");
      dbPaths.push_back(p);
    }
  }
  std::vector<std::unique_ptr<Db>> connections;
  for (const auto &p : dbPaths) {
    auto db = std::make_unique<Db>(p);
    // The repeat path only needs legacy rows. Full integrity and backup
    // verification still run before any database change is committed.
    auto snapshot = restore ? rows(*db) : migratableRows(*db);
    Json changes = Json::array();
    for (const auto &row : snapshot) {
      if (!restore && (row[1] == "custom" || !canMigrate(row[1])))
        continue;
      Json target = "custom";
      if (restore) {
        if (row[1] != "custom")
          continue;
        auto dbPlan = std::find_if(
            source["databases"].begin(), source["databases"].end(),
            [&](const Json &x) { return x.at("path") == utf8(p); });
        ensure(dbPlan != source["databases"].end(),
               "Invalid database backup entry");
        auto prior =
            std::find_if((*dbPlan)["rows"].begin(), (*dbPlan)["rows"].end(),
                         [&](const Json &x) { return x[0] == row[0]; });
        if (prior == (*dbPlan)["rows"].end())
          continue;
        target = (*prior)[1];
      }
      changes.push_back({row[0], row[1], target});
    }
    if (changes.empty())
      continue;
    plan["databases"].push_back(
        {{"path", utf8(p)},
         {"rows", changes},
         {"backup", std::to_string(connections.size()) + ".sqlite"}});
    connections.push_back(std::move(db));
  }
  if (plan["files"].empty() && connections.empty() && newConfig == oldConfig) {
    if (restore) {
      // A crash before the first write, or after the last undo write,
      // leaves nothing to change but still needs its recovery marker cleared.
      source["status"] = "restored";
      auto latest = Json::parse(read(pointer));
      write(parent / latest.at("generation").get<std::string>() /
                "manifest.json",
            source.dump(2));
      if (fs::exists(parent / "pending.json"))
        fs::remove(parent / "pending.json");
    }
    return {{"files", 0}, {"rows", 0}, {"backup", ""}, {"restored", restore}};
  }
  fs::create_directories(backup);
#ifndef _WIN32
  ::chmod(parent.c_str(), 0700);
  ::chmod(backup.c_str(), 0700);
#endif
  write(backup / "config.toml", oldConfig);
  for (const auto &edit : plan["files"]) {
    auto bytes = read(from(edit.at("path")));
    auto meta = metadata(bytes, from(edit.at("path")));
    ensure(meta.raw == edit["old_line"] && meta.offset == edit["offset"] &&
               bytes.size() == edit["size"],
           "History changed during backup");
    write(backup / edit.at("backup").get<std::string>(), bytes);
  }
  for (size_t i = 0; i < connections.size(); ++i)
    backupDb(*connections[i],
             backup / plan["databases"][i]["backup"].get<std::string>());
  write(backup / "manifest.json", plan.dump(2));
  if (!restore)
    write(parent / "pending.json", Json({{"generation", generation}}).dump());
  std::vector<size_t> changedFiles, committed;
  bool configChanged = false;
  size_t count = 0;
  try {
    for (auto &db : connections)
      db->begin();
    for (size_t i = 0; i < connections.size(); ++i)
      updateRows(*connections[i], plan["databases"][i]["rows"]);
    for (size_t i = 0; i < plan["files"].size(); ++i) {
      const auto &edit = plan["files"][i];
      auto p = from(edit.at("path"));
      auto bytes = read(p);
      auto original = read(backup / edit.at("backup").get<std::string>());
      ensure(bytes == original, "History changed after backup");
      bytes.replace(edit["offset"].get<size_t>(),
                    edit["old_line"].get<std::string>().size(),
                    edit["new_line"].get<std::string>());
      write(p, bytes);
      changedFiles.push_back(i);
      if (failAfter > 0 && int(changedFiles.size()) == failAfter)
        throw std::runtime_error("Synthetic rollback test");
    }
    ensure((fs::exists(configPath) ? read(configPath) : "") == oldConfig,
           "Configuration changed during synchronization");
    if (newConfig != oldConfig) {
      write(configPath, newConfig);
      configChanged = true;
    }
    if (failAfter == -1)
      throw std::runtime_error("Synthetic rollback after config write");
    for (size_t i = 0; i < connections.size(); ++i) {
      connections[i]->commit();
      committed.push_back(i);
      count += plan["databases"][i]["rows"].size();
      if (failAfter == -2 && i == 0)
        throw std::runtime_error("Synthetic rollback after database commit");
    }
    plan["status"] = "complete";
    write(backup / "manifest.json", plan.dump(2));
    if (!restore)
      write(parent / "latest.json", Json({{"generation", generation}}).dump());
    else {
      source["status"] = "restored";
      auto latest = Json::parse(read(pointer));
      write(parent / latest.at("generation").get<std::string>() /
                "manifest.json",
            source.dump(2));
    }
    // Marker removal is cleanup after durable completion. A removal error must
    // not roll back data after the source manifest has been marked restored.
    {
      std::error_code ignored;
      fs::remove(parent / "pending.json", ignored);
    }
  } catch (...) {
    bool rollbackOk = true;
    for (auto &db : connections)
      try {
        db->rollback();
      } catch (...) {
        rollbackOk = false;
      }
    for (auto i : committed)
      try {
        connections[i]->begin();
        updateRows(*connections[i], plan["databases"][i]["rows"], true);
        connections[i]->commit();
      } catch (...) {
        rollbackOk = false;
      }
    if (configChanged)
      try {
        ensure(read(configPath) == newConfig, "Config changed during rollback");
        write(configPath, oldConfig);
      } catch (...) {
        rollbackOk = false;
      }
    for (auto it = changedFiles.rbegin(); it != changedFiles.rend(); ++it)
      try {
        auto &edit = plan["files"][*it];
        auto p = from(edit.at("path"));
        auto bytes = read(p);
        auto meta = metadata(bytes, p);
        ensure(meta.raw == edit["new_line"],
               "Metadata changed during rollback");
        bytes.replace(meta.offset, meta.length,
                      edit["old_line"].get<std::string>());
        write(p, bytes);
      } catch (...) {
        rollbackOk = false;
      }
    plan["status"] = rollbackOk ? "rolled_back" : "recovery_required";
    try {
      write(backup / "manifest.json", plan.dump(2));
    } catch (...) {
    }
    if (!rollbackOk)
      throw std::runtime_error(
          "Rollback incomplete. Keep backups and inspect: " + utf8(backup));
    throw;
  }
  return {{"files", plan["files"].size()},
          {"rows", count},
          {"backup", utf8(backup)},
          {"restored", restore}};
}
// Recover only our pending transaction. Never restore an old configuration here:
// the caller may already have prepared a new provider before invoking sync.
Json recoverPendingLocked(const fs::path &home) {
  const auto parent = home / "yilai-history-backups";
  const auto pending = parent / "pending.json";
  noLinks(parent);
  if (!fs::exists(pending))
    return {{"recovered", false}, {"files", 0}, {"rows", 0}};
  const auto pointer = Json::parse(read(pending));
  const auto generation = pointer.at("generation").get<std::string>();
  ensure(!generation.empty() &&
             generation.find_first_not_of("0123456789-") == std::string::npos,
         "Invalid pending backup pointer");
  const auto manifestPath = parent / generation / "manifest.json";
  const auto manifest = Json::parse(read(manifestPath));
  ensure(manifest.at("home") == utf8(home),
         "History backup belongs to another directory");
  ensure(manifest.value("version", 0) == 1 &&
             manifest.value("kind", std::string()) == "sync",
         "Invalid pending history transaction");
  const auto status = manifest.value("status", std::string());
  if (status == "complete" || status == "rolled_back" || status == "restored") {
    // A completed transaction can be interrupted before latest.json is written.
    // Finish its durable bookkeeping without reverting successful history.
    if (status == "complete")
      write(parent / "latest.json", pointer.dump());
    fs::remove(pending);
    return {{"recovered", true}, {"files", 0}, {"rows", 0}};
  }
  ensure(status == "prepared" || status == "recovery_required",
         "Unknown pending history transaction status; backups left unchanged");
  auto result = operateLocked(home, true, 0, false);
  ensure(!fs::exists(pending),
         "History recovered but pending marker could not be removed; retry");
  result["recovered"] = true;
  return result;
}
Json recoverPending(const fs::path &input) {
  const auto home = fs::weakly_canonical(fs::absolute(input)).lexically_normal();
  Lock lock(home);
  return recoverPendingLocked(home);
}
Json operate(const fs::path &input, bool restore, int failAfter = 0) {
  const auto home = fs::weakly_canonical(fs::absolute(input)).lexically_normal();
  Lock lock(home);
  if (!restore)
    recoverPendingLocked(home);
  return operateLocked(home, restore, failAfter);
}
char *copy(const std::string &s) {
  auto *p = static_cast<char *>(std::malloc(s.size() + 1));
  if (p)
    std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}
void recoverySelfTest(const fs::path &root) {
  for (int scenario = 0; scenario < 5; ++scenario) {
    const auto home = root / ("recovery-" + std::to_string(scenario));
    fs::create_directories(home);
    const auto configPath = home / "config.toml";
    const std::string originalConfig =
        "model='gpt-6-astra'\nsqlite_home=" + Json(utf8(home)).dump() + "\n";
    write(configPath, originalConfig);
    for (const char *id : {"a", "b"}) {
      const Json meta = {{"type", "session_meta"},
                         {"payload", {{"id", id}, {"model_provider", "yilai"}}}};
      write(home / "sessions" / (std::string(id) + ".jsonl"), meta.dump() + "\n");
    }
    {
      Db db(home / "state_5.sqlite", true);
      db.exec("CREATE TABLE threads(id TEXT PRIMARY KEY,model_provider TEXT,title TEXT)");
      db.exec("INSERT INTO threads VALUES('a','yilai','original'),('b','yilai','original')");
    }
    operate(home, false);
    const auto parent = home / "yilai-history-backups";
    const auto pointer = read(parent / "latest.json");
    const auto manifestPath = parent /
        Json::parse(pointer).at("generation").get<std::string>() / "manifest.json";
    auto manifest = Json::parse(read(manifestPath));
    // The new switch configuration must survive recovery even when it happens
    // to match the interrupted transaction's output exactly.
    const auto candidate = scenario == 4 ? read(configPath) :
        originalConfig + "# candidate for the next switch\n";
    write(configPath, candidate);
    const auto session = home / "sessions/a.jsonl";
    const std::string newMessage =
        "{\"type\":\"event_msg\",\"payload\":{\"message\":\"added after interruption\"}}\n";
    write(session, read(session) + newMessage);
    {
      Db db(home / "state_5.sqlite");
      db.exec("UPDATE threads SET title='new title' WHERE id='a'");
    }
    if (scenario == 0) {
      // Completion persisted but latest/pending cleanup was interrupted.
      fs::remove(parent / "latest.json");
      write(parent / "pending.json", pointer);
      for (int invalid = 0; invalid < 3; ++invalid) {
        auto malformed = manifest;
        if (invalid == 0) malformed["home"] = utf8(root);
        if (invalid == 1) malformed["kind"] = "restore";
        if (invalid == 2) malformed["status"] = "unexpected";
        write(manifestPath, malformed.dump(2));
        bool rejected = false;
        try { recoverPending(home); } catch (...) { rejected = true; }
        ensure(rejected && fs::exists(parent / "pending.json") &&
                   read(configPath) == candidate &&
                   currentProvider(metadata(read(session))) == "custom",
               "Recovery accepted invalid pending ownership/kind/status");
      }
      write(manifestPath, manifest.dump(2));
      recoverPending(home);
      ensure(read(parent / "latest.json") == pointer &&
                 currentProvider(metadata(read(session))) == "custom",
             "Completed recovery reverted history or lost latest pointer");
      // Keep the ordinary manual undo contract after finishing completion.
      operate(home, true);
      ensure(currentProvider(metadata(read(session))) == "yilai" &&
                 read(session).find(newMessage) != std::string::npos,
             "Completed recovery broke undo or lost new messages");
    } else {
      manifest["status"] = scenario == 2 ? "recovery_required" : "prepared";
      write(manifestPath, manifest.dump(2));
      write(parent / "pending.json", pointer);
      // Simulate partial application, including a committed DB row. The other
      // session/row are already restored and must remain safe on retry.
      const auto other = home / "sessions/b.jsonl";
      auto bytes = read(other);
      const auto meta = metadata(bytes);
      bytes.replace(meta.offset, meta.length, changedLine(meta, "yilai"));
      write(other, bytes);
      {
        Db db(home / "state_5.sqlite");
        db.exec("UPDATE threads SET model_provider='yilai' WHERE id='b'");
      }
      if (scenario == 3) {
        // Simulate a prepared transaction before any effective writes.
        auto bytes = read(session);
        const auto meta = metadata(bytes);
        bytes.replace(meta.offset, meta.length, changedLine(meta, "yilai"));
        write(session, bytes);
        Db db(home / "state_5.sqlite");
        db.exec("UPDATE threads SET model_provider='yilai' WHERE id='a'");
      }
      if (scenario == 2) {
        // A caller that only invokes sync still gets automatic recovery.
        operate(home, false);
        ensure(currentProvider(metadata(read(session))) == "custom",
               "Sync did not recover and reapply interrupted history");
      } else {
        const auto report = recoverPending(home);
        ensure(report.at("recovered") == true &&
                   currentProvider(metadata(read(session))) == "yilai",
               "Pending recovery did not restore partial history");
        if (scenario == 3)
          ensure(report.at("files") == 0 && report.at("rows") == 0,
                 "No-write interruption recovery made unnecessary changes");
        ensure(read(configPath) == candidate,
               "Automatic recovery replaced the next switch configuration");
      }
    }
    ensure(!fs::exists(parent / "pending.json") &&
               read(session).find(newMessage) != std::string::npos,
           "Automatic recovery left pending state or lost appended messages");
    {
      Db db(home / "state_5.sqlite");
      Stmt title(db, "SELECT title FROM threads WHERE id='a'");
      ensure(sqlite3_step(title.p) == SQLITE_ROW &&
                 std::string(reinterpret_cast<const char *>(sqlite3_column_text(title.p, 0))) == "new title",
             "Automatic recovery lost a new database title");
    }
    operate(home, false);
    ensure(!fs::exists(parent / "pending.json") &&
               currentProvider(metadata(read(session))) == "custom" &&
               read(session).find(newMessage) != std::string::npos,
           "Switch after automatic recovery failed");
    ensure(recoverPending(home).at("recovered") == false,
           "Recovery without pending transaction changed state");
  }
}
void selfTest() {
  auto temp =
      fs::weakly_canonical(fs::temp_directory_path()).lexically_normal();
  auto home =
      temp / from("YilaiHistory-中文-test-" +
                  std::to_string(std::chrono::high_resolution_clock::now()
                                     .time_since_epoch()
                                     .count()));
  ensure(home.parent_path() == temp, "Unsafe test path");
  fs::create_directories(home);
  struct Cleanup {
    fs::path p;
    ~Cleanup() {
      std::error_code e;
      fs::remove_all(p, e);
    }
  } cleanup{home};
  recoverySelfTest(home);
  const auto external = home / "external-sqlite";
  fs::create_directories(external);
  // Explicit synthetic SQLite location prevents inherited CODEX_SQLITE_HOME
  // from pointing a self-test at a real user's database.
  const std::string config =
      "model='gpt-6-astra'\nsqlite_home=" + Json(utf8(external)).dump() + "\n[model_providers.other]\nname='Test'\n";
  write(home / "config.toml", config);
  write(home / "auth.json", "synthetic-auth");
  write(home / "thread_history_1.sqlite", "unrelated sentinel");
  for (int i = 0; i < 4; ++i) {
    auto file = home / (i == 3 ? "archived_sessions" : "sessions") /
                (std::to_string(i) + ".jsonl");
    std::string provider = i == 0   ? "yilai"
                           : i == 1 ? "yilai"
                           : i == 2 ? "yilai"
                                    : "yilai";
    Json meta = {
        {"type", "session_meta"},
        {"payload", {{"id", std::to_string(i)}, {"model_provider", provider}}}};
    if (i == 0)
      meta["payload"]["forked_from_id"] = "1";
    const std::string inherited = i == 0
        ? "{\"type\":\"session_meta\", \"payload\":{\"id\":\"1\",\"model_provider\":\"parent-provider\"}}\r\n"
        : "";
    write(file, meta.dump() + "\r\n" + inherited + "{\"type\":\"event_msg\",\"payload\":{"
                              "\"message\":\"unchanged text\"}}\n");
  }
  {
    Db db(home / "state_5.sqlite", true);
    db.exec("CREATE TABLE threads(id TEXT PRIMARY KEY,model_provider "
            "TEXT,title TEXT,archived INTEGER)");
    db.exec("INSERT INTO threads "
            "VALUES('0','yilai','keep',0),('1','yilai','keep2',0),('2','yilai'"
            ",'keep3',0),('3','yilai','archived',1)");
  }
  {
    Db db(external / "state_5.sqlite", true);
    db.exec("CREATE TABLE threads(id TEXT PRIMARY KEY,model_provider "
            "TEXT,title TEXT,archived INTEGER)");
    db.exec(
        "INSERT INTO threads VALUES('external','yilai','external keep',0)");
  }
  {
#ifdef _WIN32
    struct EnvironmentRestore {
      wchar_t *previous = nullptr;
      EnvironmentRestore() {
        size_t count = 0;
        _wdupenv_s(&previous, &count, L"CODEX_SQLITE_HOME");
      }
      ~EnvironmentRestore() {
        _wputenv_s(L"CODEX_SQLITE_HOME", previous ? previous : L"");
        free(previous);
      }
    } restoreEnvironment;
    ensure(_wputenv_s(L"CODEX_SQLITE_HOME", external.c_str()) == 0,
           "Cannot set synthetic SQLite environment");
#else
    struct EnvironmentRestore {
      bool hadValue = false;
      std::string previous;
      EnvironmentRestore() {
        if (const char *value = std::getenv("CODEX_SQLITE_HOME")) {
          hadValue = true;
          previous = value;
        }
      }
      ~EnvironmentRestore() {
        if (hadValue)
          ::setenv("CODEX_SQLITE_HOME", previous.c_str(), 1);
        else
          ::unsetenv("CODEX_SQLITE_HOME");
      }
    } restoreEnvironment;
    ensure(::setenv("CODEX_SQLITE_HOME", external.c_str(), 1) == 0,
           "Cannot set synthetic SQLite environment");
#endif
    auto locations = databases(home, "model='gpt-6-astra'\n");
    ensure(locations.size() == 2 &&
               std::find(locations.begin(), locations.end(),
                         external / "state_5.sqlite") != locations.end(),
           "Unicode CODEX_SQLITE_HOME was not discovered");
  }
  const auto archivedBefore = read(home / "archived_sessions/3.jsonl");
  const auto before = read(home / "sessions/0.jsonl");
  bool failed = false;
  for (int failurePoint : {1, -1, -2}) {
    failed = false;
    try {
      operate(home, false, failurePoint);
    } catch (...) {
      failed = true;
    }
    ensure(failed && read(home / "sessions/0.jsonl") == before &&
               read(home / "archived_sessions/3.jsonl") == archivedBefore &&
               read(home / "config.toml") == config,
           "Injected failure did not roll back files/config");
    {
      Db db(home / "state_5.sqlite");
      ensure(rows(db)[0][1] == "yilai", "Injected failure committed database");
    }
    {
      Db db(external / "state_5.sqlite");
      ensure(rows(db)[0][1] == "yilai",
             "Injected failure committed external database");
    }
  }
  auto synced = operate(home, false);
  const auto forkSynced = read(home / "sessions/0.jsonl");
  ensure(forkSynced.substr(forkSynced.find('\n') + 1) ==
             before.substr(before.find('\n') + 1) &&
             metadata(forkSynced).id == "0" &&
             currentProvider(metadata(forkSynced)) == "custom",
         "Fork migration changed inherited metadata or canonical identity");
  ensure(synced["files"] == 4 && synced["rows"] == 5, "Wrong migration count");
  ensure(currentProvider(metadata(read(home / "archived_sessions/3.jsonl"))) ==
             "custom",
         "Archived session was not migrated");
  {
    Db db(external / "state_5.sqlite");
    ensure(rows(db)[0][1] == "custom",
           "External SQLite location was not migrated");
  }
  ensure(read(home / "auth.json") == "synthetic-auth" &&
             read(home / "thread_history_1.sqlite") == "unrelated sentinel",
         "Unrelated files changed");
  ensure(operate(home, false)["files"] == 0, "Repeated sync not idempotent");
  auto message = "{\"type\":\"event_msg\",\"payload\":{\"message\":\"new "
                 "message after sync\"}}\n";
  write(home / "sessions/0.jsonl", read(home / "sessions/0.jsonl") + message);
  {
    Db db(home / "state_5.sqlite");
    db.exec("UPDATE threads SET title='new title' WHERE id='0'");
    db.exec("INSERT INTO threads VALUES('new','custom','new thread',0)");
  }
  operate(home, true);
  ensure(read(home / "sessions/0.jsonl") == before + message,
         "Restore changed fork metadata or lost new messages");
  {
    Db db(home / "state_5.sqlite");
    auto data = rows(db);
    ensure(data[0][1] == "yilai" && data.back()[1] == "custom",
           "Restore changed newer thread or lost source provider");
    Stmt q(db, "SELECT title FROM threads WHERE id='0'");
    ensure(sqlite3_step(q.p) == SQLITE_ROW &&
               std::string(reinterpret_cast<const char *>(
                   sqlite3_column_text(q.p, 0))) == "new title",
           "Restore lost new title");
  }
  ensure(read(home / "config.toml") == config,
         "Restore did not restore unchanged config");
  ensure(read(home / "archived_sessions/3.jsonl") == archivedBefore,
         "Restore changed archived metadata");
  {
    Db db(external / "state_5.sqlite");
    ensure(rows(db)[0][1] == "yilai", "Restore missed external database");
  }

  // Simulate an interruption before any effective writes: a prepared marker
  // must be recoverable even when all source provider values are already back.
  auto parent = home / "yilai-history-backups";
  auto latest = Json::parse(read(parent / "latest.json"));
  auto manifestPath =
      parent / latest.at("generation").get<std::string>() / "manifest.json";
  auto source = Json::parse(read(manifestPath));
  source["status"] = "prepared";
  write(manifestPath, source.dump(2));
  write(parent / "pending.json", latest.dump());
  auto recovered = operate(home, true);
  ensure(recovered["files"] == 0 && recovered["rows"] == 0 &&
             !fs::exists(parent / "pending.json"),
         "No-op recovery left a pending marker");

  // Recover a partially applied sync. Records already restored remain intact;
  // the remaining custom provider is reverted from the original manifest.
  source["status"] = "prepared";
  write(manifestPath, source.dump(2));
  write(parent / "pending.json", latest.dump());
  auto partial = read(home / "sessions/0.jsonl");
  auto partialMeta = metadata(partial);
  partial.replace(partialMeta.offset, partialMeta.length,
                  changedLine(partialMeta, "custom"));
  write(home / "sessions/0.jsonl", partial);
  operate(home, true);
  ensure(currentProvider(metadata(read(home / "sessions/0.jsonl"))) ==
                 "yilai" &&
             !fs::exists(parent / "pending.json"),
         "Partial synchronization recovery failed");

  // A malformed backup must not be allowed to modify an arbitrary database.
  auto unrelated = home / "unrelated";
  fs::create_directories(unrelated);
  {
    Db db(unrelated / "state_5.sqlite", true);
    db.exec("CREATE TABLE threads(id TEXT PRIMARY KEY,model_provider TEXT)");
    db.exec("INSERT INTO threads VALUES('external','custom')");
  }
  auto validSource = source;
  source["status"] = "prepared";
  source["databases"][0]["path"] = utf8(unrelated / "state_5.sqlite");
  write(manifestPath, source.dump(2));
  write(parent / "pending.json", latest.dump());
  failed = false;
  try {
    operate(home, true);
  } catch (...) {
    failed = true;
  }
  ensure(failed, "Undo accepted an unconfigured database path");
  {
    Db db(unrelated / "state_5.sqlite");
    ensure(rows(db)[0][1] == "custom",
           "Undo modified an unconfigured database");
  }
  write(manifestPath, validSource.dump(2));
  operate(home, true);
  write(home / "sessions/broken.jsonl", "not json");
  failed = false;
  try {
    operate(home, false);
  } catch (...) {
    failed = true;
  }
  ensure(failed && read(home / "config.toml") == config,
         "Malformed history modified configuration");
  fs::remove(home / "sessions/broken.jsonl");
  // Already-custom canonical metadata must not hide invalid history later in
  // the file, and errors must identify the file without echoing its content.
  const std::string privateTail = "private-conversation-sentinel";
  const std::string unifiedHead =
      "{\"type\":\"session_meta\",\"payload\":{\"id\":\"tail-test\",\"model_provider\":\"custom\"}}\n";
  for (const auto &tail : {privateTail,
                          std::string("{\"type\":\"session_meta\",\"payload\":{}}")}) {
    failed = false;
    try {
      metadata(unifiedHead + tail, home / "sessions/broken.jsonl");
    } catch (const std::exception &e) {
      const std::string error = e.what();
      failed = error.find("broken.jsonl") != std::string::npos &&
               error.find(privateTail) == std::string::npos;
    }
    ensure(failed, "Malformed fork tail was accepted or leaked diagnostic content");
  }
  const std::string missingProvider =
      "{\"type\":\"session_meta\",\"payload\":{\"id\":\"missing\"}}\n";
  write(home / "sessions/missing.jsonl", missingProvider);
  {
    Db db(home / "state_5.sqlite");
    db.exec(
        "INSERT INTO threads VALUES('missing',NULL,'old default provider',0)");
  }
  operate(home, false);
  operate(home, true);
  ensure(currentProvider(metadata(read(home / "sessions/missing.jsonl")))
             .is_null(),
         "Undo did not remove originally absent provider");
  {
    Db db(home / "state_5.sqlite");
    Stmt q(db, "SELECT model_provider FROM threads WHERE id='missing'");
    ensure(sqlite3_step(q.p) == SQLITE_ROW &&
               sqlite3_column_type(q.p, 0) == SQLITE_NULL,
           "Undo did not restore NULL database provider");
  }
}
} // namespace
extern "C" char *yilai_sync_history(const char *home, int restore,
                                    char **error) {
  if (error)
    *error = nullptr;
  try {
    ensure(home != nullptr, "Missing Codex home");
    auto *result = copy(operate(from(home), restore != 0).dump());
    ensure(result != nullptr, "Out of memory");
    return result;
  } catch (const std::exception &e) {
    if (error)
      *error = copy(e.what());
  } catch (...) {
    if (error)
      *error = copy("History operation failed");
  }
  return nullptr;
}
extern "C" char *yilai_recover_history(const char *home, char **error) {
  if (error)
    *error = nullptr;
  try {
    ensure(home != nullptr, "Missing Codex home");
    auto *result = copy(recoverPending(from(home)).dump());
    ensure(result != nullptr, "Out of memory");
    return result;
  } catch (const std::exception &e) {
    if (error)
      *error = copy(e.what());
  } catch (...) {
    if (error)
      *error = copy("History recovery failed");
  }
  return nullptr;
}
extern "C" int yilai_history_self_test(char **error) {
  if (error)
    *error = nullptr;
  try {
    selfTest();
    return 1;
  } catch (const std::exception &e) {
    if (error)
      *error = copy(e.what());
    return 0;
  } catch (...) {
    if (error)
      *error = copy("History self-test failed");
    return 0;
  }
}
