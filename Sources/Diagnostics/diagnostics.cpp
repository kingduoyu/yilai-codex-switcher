#include "Diagnostics.h"
#include "../HistorySync/vendor/json.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;
struct YilaiDiagnostic {
  std::string operation;
  std::string secret;
  std::string path;
  FILE *file = nullptr;
  std::mutex mutex;
  ~YilaiDiagnostic() {
    if (file)
      std::fclose(file);
  }
};
namespace {
std::atomic<unsigned long long> sequence{0};
#ifdef _WIN32
constexpr const char *platform = "Windows";
#else
constexpr const char *platform = "macOS";
#endif
char *copy_text(const std::string &value) {
  char *result = static_cast<char *>(std::malloc(value.size() + 1));
  if (result)
    std::memcpy(result, value.c_str(), value.size() + 1);
  return result;
}
std::string timestamp() {
  auto now = std::chrono::system_clock::now();
  auto seconds = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count() %
                1000;
  std::ostringstream stream;
  stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
         << std::setfill('0') << millis << 'Z';
  return stream.str();
}
std::string unique_name() {
  auto value = timestamp();
  std::replace(value.begin(), value.end(), ':', '-');
#ifdef _WIN32
  auto pid = _getpid();
#else
  auto pid = getpid();
#endif
  return value + "-" + std::to_string(pid) + "-" +
         std::to_string(sequence.fetch_add(1)) + ".log";
}
std::string sanitize(const YilaiDiagnostic *context, std::string text) {
  if (context && !context->secret.empty()) {
    size_t offset = 0;
    while ((offset = text.find(context->secret, offset)) != std::string::npos) {
      text.replace(offset, context->secret.size(), "[REDACTED]");
      offset += 10;
    }
  }
  std::string lower = text;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  // Parser errors can quote entire source lines, not only credential fields.
  // Keep only classification and numeric location, never a parser's excerpt.
  if (lower.find("parse_error") != std::string::npos ||
      lower.find("parse error") != std::string::npos ||
      lower.find("parsing") != std::string::npos ||
      lower.find("invalid toml") != std::string::npos ||
      lower.find("invalid json") != std::string::npos ||
      lower.find("syntax error") != std::string::npos) {
    std::string result =
        lower.find("toml") != std::string::npos   ? "Invalid TOML"
        : lower.find("json") != std::string::npos ? "Invalid JSON"
                                                  : "Invalid structured input";
    std::smatch match;
    const std::regex line(R"(line\s*[:=]?\s*([0-9]{1,12}))", std::regex::icase);
    const std::regex column(R"(column\s*[:=]?\s*([0-9]{1,12}))",
                            std::regex::icase);
    if (std::regex_search(text, match, line))
      result += "; line " + match[1].str();
    if (std::regex_search(text, match, column))
      result += "; column " + match[1].str();
    return result + ". Source details omitted.";
  }
  static const std::regex bearer(R"(\bBearer\s+[^\s"'<>;,}]+)",
                                 std::regex::icase);
  static const std::regex sk(R"(\bsk-[A-Za-z0-9_.-]+)", std::regex::icase);
  static const std::regex field(
      R"rx((["']?(?:OPENAI_API_KEY|api[_-]?key|experimental_bearer_token|access[_-]?token|refresh[_-]?token|id[_-]?token|session[_-]?token|auth[_-]?token|personal_access_token|client[_-]?secret|secret[_-]?key|token|authorization|password|secret)["']?\s*[:=]\s*)(?:"(?:\\.|[^"\\])*"|'[^']*'|[^\r\n,;}]+))rx",
      std::regex::icase);
  text = std::regex_replace(text, field, "$1[REDACTED]");
  text = std::regex_replace(text, bearer, "Bearer [REDACTED]");
  text = std::regex_replace(text, sk, "[REDACTED]");
  // Keep the log bounded; sanitization happens before truncation so a long
  // error cannot expose the prefix of a credential at the truncation boundary.
  if (text.size() > 4096) {
    size_t end = 4096;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
      --end;
    text = text.substr(0, end) + " [truncated]";
  }
  return text;
}
void record(YilaiDiagnostic *context, const char *stage, const char *message,
            const char *result) {
  if (!context || !context->file)
    return;
  std::lock_guard<std::mutex> guard(context->mutex);
  json entry{{"version", "3.3.1"},
             {"platform", platform},
             {"time", timestamp()},
             {"action", sanitize(context, context->operation)},
             {"step", sanitize(context, stage ? stage : "")},
             {"result", result},
             {"message", sanitize(context, message ? message : "")}};
  if (std::strcmp(result, "failure") == 0)
    entry["error"] = entry["message"];
  auto line = entry.dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
  if (std::fwrite(line.data(), 1, line.size(), context->file) != line.size() ||
      std::fflush(context->file) != 0) {
    // Log failures must not alter the operation's actual success/failure.
    std::fclose(context->file);
    context->file = nullptr;
  }
}
FILE *create_log(const fs::path &path) {
#ifdef _WIN32
  int fd = -1;
  if (_wsopen_s(&fd, path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                _SH_DENYWR, _S_IREAD | _S_IWRITE) != 0)
    return nullptr;
  auto *file = _fdopen(fd, "wb");
  if (!file)
    _close(fd);
#else
  int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
  if (fd < 0)
    return nullptr;
  auto *file = fdopen(fd, "wb");
  if (!file)
    close(fd);
#endif
  return file;
}
void open_log(YilaiDiagnostic &context, const char *home) {
  if (!home || !*home)
    return;
  auto directory = fs::u8path(home) / "yilai-switcher-logs";
  std::error_code error;
  fs::create_directory(directory, error);
  if (error)
    return;
  auto status = fs::symlink_status(directory, error);
  if (error || !fs::is_directory(status) || fs::is_symlink(status))
    return;
#ifndef _WIN32
  if (chmod(directory.c_str(), 0700) != 0)
    return;
#endif
  for (int attempt = 0; attempt < 3; ++attempt) {
    auto path = directory / unique_name();
    auto *file = create_log(path);
    if (!file)
      continue;
    context.file = file;
    context.path = path.u8string();
    return;
  }
}
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void self_test() {
  auto home =
      fs::temp_directory_path() / ("yilai-diagnostics-test-" + unique_name());
  fs::create_directory(home);
  struct Cleanup {
    fs::path path;
    ~Cleanup() {
      std::error_code error;
      fs::remove_all(path, error);
    }
  } cleanup{home};
  const std::string key = "synthetic-exact-secret-123";
  std::vector<std::string> paths;
  for (int success : {1, 0}) {
    auto *context = yilai_diagnostic_begin(home.u8string().c_str(),
                                           "test-operation", key.c_str());
    check(context && *yilai_diagnostic_path(context),
          "Diagnostic log was not created.");
    paths.emplace_back(yilai_diagnostic_path(context));
    const char *sensitive =
        "synthetic-exact-secret-123 sk-otherSecret_123 Bearer bearer-material "
        "access_token=access-material\nrefresh_token: 'refresh-material'\n"
        "{\"id_token\":\"identity-material\",\"password\":\"password-"
        "material\"}";
    auto *safe = yilai_diagnostic_sanitize(context, sensitive);
    check(safe != nullptr, "Diagnostic sanitizer allocation failed.");
    auto sanitized = std::string(safe);
    std::free(safe);
    for (const char *secret :
         {key.c_str(), "sk-otherSecret_123", "bearer-material",
          "access-material", "refresh-material", "identity-material",
          "password-material"})
      check(sanitized.find(secret) == std::string::npos,
            "Diagnostic secret redaction failed.");
    yilai_diagnostic_event(context, "config", sensitive);
    yilai_diagnostic_event(context, "parse",
                           "[json.exception.parse_error.101] parse error at "
                           "line 8, column 23: 'private-user-message'");
    yilai_diagnostic_event(
        context, "parse",
        "Error while parsing TOML at line 9 column 7: raw-source-fragment");
    yilai_diagnostic_end(context, success, success ? "Completed" : sensitive);
  }
  check(paths[0] != paths[1], "Diagnostic paths are not unique.");
  for (size_t index = 0; index < paths.size(); ++index) {
    std::ifstream input(fs::u8path(paths[index]));
    std::string line;
    int rows = 0;
    while (std::getline(input, line)) {
      auto entry = json::parse(line);
      check(entry["version"] == "3.3.1" && entry["platform"] == platform &&
                entry.contains("time") && entry["action"] == "test-operation",
            "Diagnostic fields missing.");
      check(line.find(key) == std::string::npos &&
                line.find("private-user-message") == std::string::npos &&
                line.find("raw-source-fragment") == std::string::npos &&
                line.find("access-material") == std::string::npos,
            "Diagnostic file leaked sensitive error details.");
      if (entry["step"] == "end")
        check(entry["result"] == (index == 0 ? "success" : "failure"),
              "Diagnostic outcome does not reflect the business result.");
      ++rows;
    }
    check(rows == 5, "Diagnostic events missing.");
#ifndef _WIN32
    struct stat info{};
    check(stat(paths[index].c_str(), &info) == 0 &&
              (info.st_mode & 0777) == 0600,
          "Diagnostic file permissions are not private.");
#endif
  }
#ifndef _WIN32
  struct stat info{};
  check(stat((home / "yilai-switcher-logs").c_str(), &info) == 0 &&
            (info.st_mode & 0777) == 0700,
        "Diagnostic directory permissions are not private.");
#endif
  auto *extra_safe = yilai_diagnostic_sanitize(
      nullptr, "'token': 'single-key-material'\nclient_secret=client-material\n"
               "sessionToken=session-material\napiKey=api-material");
  check(extra_safe != nullptr, "Generic diagnostic sanitizer failed.");
  auto extra_text = std::string(extra_safe);
  std::free(extra_safe);
  for (const char *secret : {"single-key-material", "client-material",
                             "session-material", "api-material"})
    check(extra_text.find(secret) == std::string::npos,
          "A common credential field was not redacted.");
  auto *disabled = yilai_diagnostic_begin(
      (home / "missing" / "home").u8string().c_str(), "test", key.c_str());
  check(!disabled || !*yilai_diagnostic_path(disabled),
        "Unavailable diagnostic location unexpectedly succeeded.");
  yilai_diagnostic_event(disabled, "step", "must not fail the operation");
  yilai_diagnostic_end(disabled, 0, "Failed operation remains failed");
}
} // namespace
extern "C" YilaiDiagnostic *yilai_diagnostic_begin(const char *home,
                                                   const char *operation,
                                                   const char *secret) {
  try {
    auto context = std::make_unique<YilaiDiagnostic>();
    context->operation = operation ? operation : "unknown";
    context->secret = secret ? secret : "";
    try {
      open_log(*context, home);
      record(context.get(), "begin", "Operation started", "started");
    } catch (
        ...) { /* A usable redaction context remains even if logging failed. */
    }
    return context.release();
  } catch (...) {
    return nullptr;
  }
}
extern "C" void yilai_diagnostic_event(YilaiDiagnostic *context,
                                       const char *stage, const char *message) {
  try {
    record(context, stage, message, "progress");
  } catch (...) {
  }
}
extern "C" const char *yilai_diagnostic_path(const YilaiDiagnostic *context) {
  return context ? context->path.c_str() : "";
}
extern "C" char *yilai_diagnostic_sanitize(const YilaiDiagnostic *context,
                                           const char *message) {
  try {
    return copy_text(sanitize(context, message ? message : ""));
  } catch (...) {
    return copy_text(
        "Operation failed. Diagnostic details could not be sanitized.");
  }
}
extern "C" void yilai_diagnostic_end(YilaiDiagnostic *context, int success,
                                     const char *message) {
  try {
    record(context, "end", message, success ? "success" : "failure");
  } catch (...) {
  }
  delete context;
}
extern "C" int yilai_diagnostic_self_test(char **error) {
  if (error)
    *error = nullptr;
  try {
    self_test();
    return 1;
  } catch (const std::exception &failure) {
    if (error)
      *error = copy_text(failure.what());
  } catch (...) {
    if (error)
      *error = copy_text("Diagnostics self-test failed.");
  }
  return 0;
}
