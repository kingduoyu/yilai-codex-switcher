#include "Diagnostics.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>

struct YilaiDiagnostic {
  std::string secret;
};

namespace {
char *copy_text(const std::string &value) {
  char *result = static_cast<char *>(std::malloc(value.size() + 1));
  if (result)
    std::memcpy(result, value.c_str(), value.size() + 1);
  return result;
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
  // Parser failures may include the original line, including credentials.
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
    const std::regex line(R"(line\s*[:=]?\s*([0-9]{1,12}))",
                          std::regex::icase);
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
  if (text.size() > 4096) {
    size_t end = 4096;
    while (end > 0 &&
           (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
      --end;
    text = text.substr(0, end) + " [truncated]";
  }
  return text;
}

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void self_test() {
  const std::string key = "synthetic-exact-secret-123";
  auto *context = yilai_diagnostic_begin(nullptr, "test-operation", key.c_str());
  check(context != nullptr, "Error sanitizer context was not created.");
  const char *sensitive =
      "synthetic-exact-secret-123 sk-otherSecret_123 Bearer bearer-material "
      "access_token=access-material\nrefresh_token: 'refresh-material'\n"
      "{\"id_token\":\"identity-material\",\"password\":\"password-material\"}";
  auto *safe = yilai_diagnostic_sanitize(context, sensitive);
  check(safe != nullptr, "Error sanitizer allocation failed.");
  auto sanitized = std::string(safe);
  std::free(safe);
  for (const char *secret :
       {key.c_str(), "sk-otherSecret_123", "bearer-material",
        "access-material", "refresh-material", "identity-material",
        "password-material"})
    check(sanitized.find(secret) == std::string::npos,
          "Displayed error sanitizer leaked a credential.");
  check(*yilai_diagnostic_path(context) == '\0' &&
            !yilai_diagnostic_available(context),
        "Error-only diagnostics unexpectedly created a log.");
  check(!yilai_diagnostic_finish(context, 0, sensitive),
        "Error-only diagnostics reported a saved log.");

  auto *parse = yilai_diagnostic_sanitize(
      nullptr, "TOML parse error at line 9 column 7: sk-private-value");
  check(parse != nullptr, "Parser error sanitizer failed.");
  auto parseText = std::string(parse);
  std::free(parse);
  check(parseText.find("sk-private-value") == std::string::npos &&
            parseText.find("line 9") != std::string::npos,
        "Parser error sanitizer exposed source text or lost its location.");
}
} // namespace

extern "C" YilaiDiagnostic *yilai_diagnostic_begin(const char *, const char *,
                                                     const char *secret) {
  try {
    auto context = std::make_unique<YilaiDiagnostic>();
    context->secret = secret ? secret : "";
    return context.release();
  } catch (...) {
    return nullptr;
  }
}

extern "C" void yilai_diagnostic_event(YilaiDiagnostic *, const char *,
                                        const char *) {}

extern "C" const char *yilai_diagnostic_path(const YilaiDiagnostic *) {
  return "";
}

extern "C" char *yilai_diagnostic_sanitize(const YilaiDiagnostic *context,
                                             const char *message) {
  try {
    return copy_text(sanitize(context, message ? message : ""));
  } catch (...) {
    return copy_text("操作失败，错误详情无法安全显示。");
  }
}

extern "C" int yilai_diagnostic_available(const YilaiDiagnostic *) { return 0; }

extern "C" int yilai_diagnostic_finish(YilaiDiagnostic *context, int,
                                         const char *) {
  delete context;
  return 0;
}

extern "C" void yilai_diagnostic_end(YilaiDiagnostic *context, int success,
                                      const char *message) {
  (void)yilai_diagnostic_finish(context, success, message);
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
