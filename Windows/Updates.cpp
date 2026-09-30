#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <winver.h>

#include "Updates.h"
#include "resource.h"
#include "../Sources/Shared/update_protocol.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <stdexcept>
#include <vector>

namespace app {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Json = updates::Json;
constexpr std::size_t MaxAsset = 100 * 1024 * 1024;
constexpr std::size_t MaxCatalog = 4 * 1024 * 1024;
constexpr char AssetName[] = "YilaiCodexSwitcher.exe";

void require(bool value, const char *message) { updates::require(value, message); }
void winCheck(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(std::string(message) + " (Windows error " +
                             std::to_string(GetLastError()) + ")");
}
std::wstring wide(const std::string &text) {
  if (text.empty()) return {};
  require(text.size() <= INT_MAX && text.find('\0') == std::string::npos,
          "Invalid UTF-8 text");
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                  int(text.size()), nullptr, 0);
  require(n > 0, "Invalid UTF-8 text");
  std::wstring out(std::size_t(n), L'\0');
  winCheck(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                              int(text.size()), out.data(), n) == n,
           "Cannot decode UTF-8");
  return out;
}
std::string utf8(const std::wstring &text) {
  if (text.empty()) return {};
  require(text.size() <= INT_MAX && text.find(L'\0') == std::wstring::npos,
          "Invalid Unicode text");
  const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                  int(text.size()), nullptr, 0, nullptr, nullptr);
  require(n > 0, "Invalid Unicode text");
  std::string out(std::size_t(n), '\0');
  winCheck(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                              int(text.size()), out.data(), n, nullptr, nullptr) == n,
           "Cannot encode UTF-8");
  return out;
}
struct Handle {
  HANDLE value;
  explicit Handle(HANDLE h) : value(h) {}
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
};
struct Internet {
  HINTERNET value;
  explicit Internet(HINTERNET h) : value(h) {
    winCheck(h != nullptr, "Cannot open HTTPS handle");
  }
  ~Internet() { WinHttpCloseHandle(value); }
  Internet(const Internet &) = delete;
  Internet &operator=(const Internet &) = delete;
};

struct Url { std::wstring host, object; };
Url approvedUrl(const std::string &url) {
  require(url.size() <= 8192 && url.rfind("https://", 0) == 0,
          "Only approved HTTPS URLs are allowed");
  for (char value : url) {
    const auto c = static_cast<unsigned char>(value);
    require(c > 32 && c < 127 && c != '\\' && c != '#', "Invalid HTTPS URL");
  }
  const auto text = wide(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
  parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
  winCheck(WinHttpCrackUrl(text.c_str(), DWORD(text.size()), 0, &parts) != FALSE,
           "Cannot parse HTTPS URL");
  require(parts.nScheme == INTERNET_SCHEME_HTTPS && parts.nPort == 443 &&
              parts.dwUserNameLength == 0 && parts.dwPasswordLength == 0,
          "HTTPS URL contains an unexpected port or credentials");
  std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  for (auto &c : host) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
  const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  const std::wstring query = parts.dwExtraInfoLength
      ? std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength) : L"";
  require(path.find(L'%') == std::wstring::npos &&
              path.find(L"//") == std::wstring::npos &&
              path.find(L"/./") == std::wstring::npos &&
              path.find(L"/../") == std::wstring::npos &&
              path.size() >= 1 && path.back() != L'.', "Invalid HTTPS path");
  bool allowed = false;
  if (host == L"api.github.com")
    allowed = path == L"/repos/kingduoyu/yilai-codex-switcher/releases/latest" && query.empty();
  else if (host == L"github.com")
    allowed = query.empty() && std::regex_match(path, std::wregex(
        LR"(/kingduoyu/yilai-codex-switcher/releases/download/v?[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}/YilaiCodexSwitcher\.exe)"));
  else if (host == L"raw.githubusercontent.com")
    allowed = query.empty() && std::regex_match(path, std::wregex(
        LR"(/kingduoyu/yilai-codex-switcher/(main/model-channel\.json|[a-f0-9]{40}/model-catalog\.json))"));
  else if (host == L"release-assets.githubusercontent.com" ||
           host == L"objects.githubusercontent.com" ||
           host == L"github-releases.githubusercontent.com")
    allowed = std::regex_match(path, std::wregex(
        LR"(/github-production-release-asset(-[a-zA-Z0-9]+)?/[a-zA-Z0-9/._-]+)"));
  require(allowed, "HTTPS URL is outside the approved GitHub update endpoints");
  return {host, path + query};
}
std::string redirectUrl(const std::string &current, const std::wstring &location) {
  auto next = utf8(location);
  if (!next.empty() && next.front() == '/' && next.rfind("//", 0) != 0) {
    const auto slash = current.find('/', 8);
    require(slash != std::string::npos, "Invalid HTTPS redirect");
    next = current.substr(0, slash) + next;
  }
  approvedUrl(next);
  return next;
}

struct AsyncState {
  Handle event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  std::atomic<DWORD> status{0}, error{0}, count{0};
  std::array<char, 16384> buffer{};
  AsyncState() { winCheck(event.value != nullptr, "Cannot create HTTPS event"); }
  void reset() {
    status = error = count = 0;
    winCheck(ResetEvent(event.value) != FALSE, "Cannot reset HTTPS event");
  }
};
struct AsyncContext { std::shared_ptr<AsyncState> state; };
void CALLBACK completion(HINTERNET, DWORD_PTR context, DWORD status,
                         LPVOID information, DWORD length) noexcept {
  if (!context) return;
  auto *binding = reinterpret_cast<AsyncContext *>(context);
  if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
    delete binding;
    return;
  }
  const auto state = binding->state;
  if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
    state->error = information && length >= sizeof(WINHTTP_ASYNC_RESULT)
        ? static_cast<WINHTTP_ASYNC_RESULT *>(information)->dwError : ERROR_INVALID_DATA;
  } else if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
             status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
             status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE) return;
  state->count = status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE ? length : 0;
  state->status = status;
  SetEvent(state->event.value);
}
void waitHttps(const std::shared_ptr<AsyncState> &state, DWORD expected,
               Clock::time_point deadline, DWORD operationMs) {
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
  require(left > 0, "HTTPS total deadline exceeded");
  const auto result = WaitForSingleObject(state->event.value,
      DWORD(std::min<long long>(left, operationMs)));
  require(result != WAIT_TIMEOUT, "HTTPS request timed out");
  winCheck(result == WAIT_OBJECT_0, "Cannot wait for HTTPS request");
  if (state->error)
    throw std::runtime_error("HTTPS request failed (Windows error " +
                             std::to_string(state->error.load()) + ")");
  require(state->status == expected && Clock::now() < deadline,
          "HTTPS total deadline exceeded or unexpected completion");
}
std::optional<std::wstring> header(HINTERNET request, DWORD kind) {
  DWORD bytes = 0;
  if (WinHttpQueryHeaders(request, kind, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                          &bytes, WINHTTP_NO_HEADER_INDEX)) return L"";
  if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) return std::nullopt;
  require(GetLastError() == ERROR_INSUFFICIENT_BUFFER && bytes <= 65536 &&
              bytes % sizeof(wchar_t) == 0, "Invalid HTTPS response header");
  std::wstring out(bytes / sizeof(wchar_t), L'\0');
  winCheck(WinHttpQueryHeaders(request, kind, WINHTTP_HEADER_NAME_BY_INDEX,
                              out.data(), &bytes, WINHTTP_NO_HEADER_INDEX) != FALSE,
           "Cannot read HTTPS response header");
  while (!out.empty() && out.back() == L'\0') out.pop_back();
  require(out.find(L'\0') == std::wstring::npos, "Invalid HTTPS response header");
  return out;
}
std::size_t responseSize(const std::wstring &value, std::size_t limit) {
  require(!value.empty(), "Invalid HTTPS content length");
  std::size_t size = 0;
  for (auto c : value) {
    require(c >= L'0' && c <= L'9', "Invalid HTTPS content length");
    const auto digit = std::size_t(c - L'0');
    require(digit <= limit && size <= (limit - digit) / 10,
            "HTTPS response exceeds the size limit");
    size = size * 10 + digit;
  }
  return size;
}

fs::path absolutePath(const fs::path &path) {
  const auto value = path.wstring();
  require(value.size() >= 3 && value.size() < MAX_PATH &&
              ((value[0] >= L'A' && value[0] <= L'Z') || (value[0] >= L'a' && value[0] <= L'z')) &&
              value[1] == L':' && value[2] == L'\\' &&
              value.find(L':', 2) == std::wstring::npos &&
              value.find(L'\0') == std::wstring::npos &&
              path.lexically_normal() == path, "Unsafe update filesystem path");
  for (const auto &part : path.relative_path())
    require(!part.empty() && part != L"." && part != L".." &&
                part.wstring().back() != L'.' && part.wstring().back() != L' ',
            "Unsafe update filesystem component");
  return path;
}
void safePath(const fs::path &path, bool directory, bool missing = false) {
  absolutePath(path);
  fs::path current = path.root_path();
  auto check = [&](bool isDirectory, bool allowMissing) {
    const auto attributes = GetFileAttributesW(current.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const auto code = GetLastError();
      require(allowMissing && (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND),
              "Update path is inaccessible");
      return;
    }
    require(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                bool(attributes & FILE_ATTRIBUTE_DIRECTORY) == isDirectory,
            "Update paths must be regular files/directories without reparse points");
  };
  check(true, false);
  for (const auto &part : path.relative_path()) {
    current /= part;
    const bool last = current == path;
    check(last ? directory : true, last && missing);
  }
}
fs::path executable() {
  std::vector<wchar_t> path(32768);
  const auto n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
  winCheck(n > 0 && n < path.size(), "Cannot locate running executable");
  return absolutePath(fs::path(std::wstring(path.data(), n)));
}
fs::path environmentPath(const wchar_t *name) {
  const auto n = GetEnvironmentVariableW(name, nullptr, 0);
  require(n > 1 && n < MAX_PATH, "Required update environment directory is unavailable");
  std::wstring text(n, L'\0');
  const auto length = GetEnvironmentVariableW(name, text.data(), n);
  winCheck(length > 0 && length < n, "Cannot read update environment directory");
  text.resize(length);
  return absolutePath(fs::path(text));
}
fs::path systemDirectory(bool windows = false) {
  std::array<wchar_t, MAX_PATH> text{};
  const auto n = windows ? GetWindowsDirectoryW(text.data(), DWORD(text.size()))
                         : GetSystemDirectoryW(text.data(), DWORD(text.size()));
  winCheck(n > 0 && n < text.size(), "Cannot locate Windows system directory");
  return absolutePath(fs::path(std::wstring(text.data(), n)));
}
void makeDirectory(const fs::path &path) {
  safePath(path.parent_path(), true);
  winCheck(CreateDirectoryW(path.c_str(), nullptr) != FALSE, "Cannot create update directory");
  safePath(path, true);
}
fs::path resultDirectory(bool create) {
  const auto local = environmentPath(L"LOCALAPPDATA");
  safePath(local, true);
  const auto path = absolutePath(local / L"YilaiCodexSwitcher");
  if (create && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) makeDirectory(path);
  safePath(path, true, !create);
  return path;
}
void writeNew(const fs::path &path, const std::string &data) {
  safePath(path, false, true);
  Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
  winCheck(file.value != INVALID_HANDLE_VALUE, "Update directory is not writable");
  DWORD written = 0;
  winCheck(data.size() <= MAXDWORD &&
               WriteFile(file.value, data.data(), DWORD(data.size()), &written, nullptr) &&
               written == data.size() && FlushFileBuffers(file.value),
           "Cannot stage update file");
}
std::string readHandle(HANDLE file, std::size_t limit) {
  BY_HANDLE_FILE_INFORMATION attributes{};
  winCheck(GetFileInformationByHandle(file, &attributes) != FALSE, "Cannot inspect update file");
  require(GetFileType(file) == FILE_TYPE_DISK &&
              !(attributes.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)),
          "Expected a regular update file");
  const auto size = (std::uint64_t(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
  require(size <= limit, "Update file exceeds the size limit");
  std::string data(std::size_t(size), '\0');
  std::size_t offset = 0;
  while (offset < data.size()) {
    DWORD n = 0;
    const auto count = DWORD(std::min<std::size_t>(data.size() - offset, 65536));
    winCheck(ReadFile(file, data.data() + offset, count, &n, nullptr) != FALSE,
             "Cannot read update file");
    require(n > 0, "Update file was truncated");
    offset += n;
  }
  char extra;
  DWORD n = 0;
  winCheck(ReadFile(file, &extra, 1, &n, nullptr) != FALSE, "Cannot finish reading update file");
  require(n == 0, "Update file changed while reading");
  return data;
}
std::string readFile(const fs::path &path, std::size_t limit) {
  safePath(path, false);
  Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  winCheck(file.value != INVALID_HANDLE_VALUE, "Cannot open update file");
  return readHandle(file.value, limit);
}
std::string hex(const unsigned char *data, std::size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    result += digits[data[i] >> 4]; result += digits[data[i] & 15];
  }
  return result;
}
std::string token() {
  std::array<unsigned char, 16> data{};
  require(BCryptGenRandom(nullptr, data.data(), ULONG(data.size()),
                         BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0, "Cannot create update identifier");
  return hex(data.data(), data.size());
}
std::string base64(const std::string &data) {
  constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (std::size_t i = 0; i < data.size(); i += 3) {
    const auto a = static_cast<unsigned char>(data[i]);
    const auto b = i + 1 < data.size() ? static_cast<unsigned char>(data[i + 1]) : 0;
    const auto c = i + 2 < data.size() ? static_cast<unsigned char>(data[i + 2]) : 0;
    out += alphabet[a >> 2]; out += alphabet[((a & 3) << 4) | (b >> 4)];
    out += i + 1 < data.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
    out += i + 2 < data.size() ? alphabet[c & 63] : '=';
  }
  return out;
}
std::wstring quoteArgument(const std::wstring &text) {
  require(text.find(L'\0') == std::wstring::npos, "Invalid process argument");
  std::wstring result = L"\"";
  std::size_t slashes = 0;
  for (auto c : text) {
    if (c == L'\\') { ++slashes; continue; }
    result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    result += c;
  }
  result.append(slashes * 2, L'\\');
  return result + L'"';
}
template <typename T> T structure(const std::string &data, std::size_t offset) {
  require(offset <= data.size() && sizeof(T) <= data.size() - offset,
          "Truncated Windows executable");
  T value{};
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}
void validatePE(const std::string &data) {
  const auto dos = structure<IMAGE_DOS_HEADER>(data, 0);
  require(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= LONG(sizeof(dos)),
          "Update is not a Windows executable");
  const auto offset = std::size_t(dos.e_lfanew);
  require(structure<DWORD>(data, offset) == IMAGE_NT_SIGNATURE, "Invalid PE signature");
  const auto file = structure<IMAGE_FILE_HEADER>(data, offset + sizeof(DWORD));
#if defined(__aarch64__) || defined(_M_ARM64)
  constexpr WORD machine = IMAGE_FILE_MACHINE_ARM64;
#elif defined(_WIN64)
  constexpr WORD machine = IMAGE_FILE_MACHINE_AMD64;
#else
  constexpr WORD machine = IMAGE_FILE_MACHINE_I386;
#endif
  require(file.Machine == machine && file.NumberOfSections > 0 && file.NumberOfSections <= 96 &&
              (file.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) &&
              !(file.Characteristics & IMAGE_FILE_DLL) &&
              file.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER),
          "Update executable has the wrong architecture or type");
  const auto optionalOffset = offset + sizeof(DWORD) + sizeof(file);
  const auto optional = structure<IMAGE_OPTIONAL_HEADER>(data, optionalOffset);
  require(optional.Magic == IMAGE_NT_OPTIONAL_HDR_MAGIC &&
              optional.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI &&
              optional.SizeOfImage > 0 && optional.SizeOfImage <= 512 * 1024 * 1024 &&
              optional.SizeOfHeaders <= data.size() &&
              optional.AddressOfEntryPoint > 0 && optional.AddressOfEntryPoint < optional.SizeOfImage,
          "Invalid Windows application image");
  bool entry = false;
  for (WORD i = 0; i < file.NumberOfSections; ++i) {
    const auto section = structure<IMAGE_SECTION_HEADER>(data,
        optionalOffset + file.SizeOfOptionalHeader + i * sizeof(IMAGE_SECTION_HEADER));
    require(section.PointerToRawData <= data.size() &&
                section.SizeOfRawData <= data.size() - section.PointerToRawData &&
                section.VirtualAddress <= optional.SizeOfImage &&
                section.Misc.VirtualSize <= optional.SizeOfImage - section.VirtualAddress,
            "Truncated PE section");
    if (optional.AddressOfEntryPoint >= section.VirtualAddress &&
        optional.AddressOfEntryPoint - section.VirtualAddress < section.Misc.VirtualSize &&
        (section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) entry = true;
  }
  require(entry, "Invalid PE entry point");
}
void validateVersion(const fs::path &path, const std::string &version) {
  // Resource-only loading never invokes the downloaded executable's entry point.
  const auto module = LoadLibraryExW(path.c_str(), nullptr,
      LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  winCheck(module != nullptr, "Cannot inspect update version resources");
  struct Module { HMODULE value; ~Module() { FreeLibrary(value); } } guard{module};
  const auto resource = FindResourceW(module, MAKEINTRESOURCEW(VS_VERSION_INFO), RT_VERSION);
  require(resource != nullptr, "Update executable has no version resource");
  const auto loaded = LoadResource(module, resource);
  const auto bytes = SizeofResource(module, resource);
  const auto *data = static_cast<const unsigned char *>(LockResource(loaded));
  require(data && bytes >= 6, "Invalid executable version resource");
  WORD length = 0, valueLength = 0, type = 0;
  std::memcpy(&length, data, 2); std::memcpy(&valueLength, data + 2, 2); std::memcpy(&type, data + 4, 2);
  constexpr wchar_t key[] = L"VS_VERSION_INFO";
  require(length <= bytes && length >= 6 + sizeof(key) && type == 0 &&
              valueLength == sizeof(VS_FIXEDFILEINFO) &&
              std::memcmp(data + 6, key, sizeof(key)) == 0,
          "Invalid executable version information");
  const auto offset = (6 + sizeof(key) + 3) & ~std::size_t(3);
  require(offset <= length && sizeof(VS_FIXEDFILEINFO) <= length - offset,
          "Truncated executable version information");
  VS_FIXEDFILEINFO fixed{};
  std::memcpy(&fixed, data + offset, sizeof(fixed));
  const auto expected = updates::version(version);
  require(fixed.dwSignature == VS_FFI_SIGNATURE && fixed.dwFileType == VFT_APP &&
              fixed.dwFileOS == VOS_NT_WINDOWS32 &&
              HIWORD(fixed.dwFileVersionMS) == expected[0] &&
              LOWORD(fixed.dwFileVersionMS) == expected[1] &&
              HIWORD(fixed.dwFileVersionLS) == expected[2] && LOWORD(fixed.dwFileVersionLS) == 0 &&
              fixed.dwProductVersionMS == fixed.dwFileVersionMS &&
              fixed.dwProductVersionLS == fixed.dwFileVersionLS,
          "Executable version does not match the requested release");
}
void cleanStage(const fs::path &stage) noexcept {
  try {
    absolutePath(stage);
    require(std::regex_match(stage.filename().wstring(),
                            std::wregex(L"\\.yilai-update-[a-f0-9]{32}")), "Unsafe update cleanup");
    safePath(stage, true);
    for (const auto &item : fs::recursive_directory_iterator(stage))
      safePath(item.path(), item.is_directory());
    fs::remove_all(stage);
  } catch (...) {}
}
void candidateSelfTest(const fs::path &source, const fs::path &stage) {
  const auto root = stage / L"self-test";
  makeDirectory(root);
  for (const auto *name : {L"home", L"local", L"roaming", L"temp", L"codex"})
    makeDirectory(root / name);
  const auto windows = systemDirectory(true);
  const auto system = systemDirectory();
  std::map<std::wstring, std::wstring> variables{
      {L"APPDATA", (root / L"roaming").wstring()}, {L"CODEX_HOME", (root / L"codex").wstring()},
      {L"HOME", (root / L"home").wstring()}, {L"HOMEDRIVE", source.root_name().wstring()},
      {L"HOMEPATH", (root / L"home").relative_path().wstring()},
      {L"LOCALAPPDATA", (root / L"local").wstring()}, {L"PATH", system.wstring()},
      {L"SystemRoot", windows.wstring()}, {L"TEMP", (root / L"temp").wstring()},
      {L"TMP", (root / L"temp").wstring()}, {L"USERPROFILE", (root / L"home").wstring()},
      {L"WINDIR", windows.wstring()}};
  variables[L"HOMEPATH"] = L"\\" + variables[L"HOMEPATH"];
  std::vector<wchar_t> environment;
  for (const auto &item : variables) {
    const auto value = item.first + L"=" + item.second;
    environment.insert(environment.end(), value.begin(), value.end());
    environment.push_back(L'\0');
  }
  environment.push_back(L'\0');
  Handle job(CreateJobObjectW(nullptr, nullptr));
  winCheck(job.value != nullptr, "Cannot isolate update self-test");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  winCheck(SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                   &limits, sizeof(limits)) != FALSE,
           "Cannot bound update self-test processes");
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process{};
  auto command = quoteArgument(source.wstring()) + L" --self-test";
  winCheck(CreateProcessW(source.c_str(), command.data(), nullptr, nullptr, FALSE,
      CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
      environment.data(), root.c_str(), &startup, &process) != FALSE,
      "Cannot start update self-test");
  Handle child(process.hProcess), thread(process.hThread);
  if (!AssignProcessToJobObject(job.value, child.value)) {
    TerminateProcess(child.value, 1);
    throw std::runtime_error("Cannot isolate update self-test process");
  }
  winCheck(ResumeThread(thread.value) != DWORD(-1), "Cannot resume update self-test");
  const auto waited = WaitForSingleObject(child.value, 45000);
  if (waited != WAIT_OBJECT_0) {
    TerminateJobObject(job.value, 1);
    WaitForSingleObject(child.value, 3000);
    throw std::runtime_error("Update self-test timed out or could not be completed");
  }
  DWORD code = 1;
  winCheck(GetExitCodeProcess(child.value, &code) != FALSE, "Cannot read update self-test result");
  require(code == 0, "Downloaded executable did not pass its isolated self-test");
}
bool samePath(const std::wstring &left, const std::wstring &right) {
  return CompareStringOrdinal(left.c_str(), int(left.size()), right.c_str(),
                              int(right.size()), TRUE) == CSTR_EQUAL;
}
} // namespace

std::string sha256(const std::string &data) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
          "Cannot initialize SHA-256");
  struct Algorithm { BCRYPT_ALG_HANDLE value; ~Algorithm() { BCryptCloseAlgorithmProvider(value, 0); } } a{algorithm};
  DWORD bytes = 0, objectSize = 0;
  require(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                           reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &bytes, 0) >= 0 &&
              bytes == sizeof(objectSize), "Cannot initialize SHA-256 storage");
  std::vector<unsigned char> object(objectSize);
  BCRYPT_HASH_HANDLE hash = nullptr;
  require(BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0,
          "Cannot create SHA-256 hash");
  struct Hash { BCRYPT_HASH_HANDLE value; ~Hash() { BCryptDestroyHash(value); } } h{hash};
  for (std::size_t offset = 0; offset < data.size();) {
    const auto count = ULONG(std::min<std::size_t>(data.size() - offset, 1024 * 1024));
    require(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(data.data() + offset)),
                           count, 0) >= 0, "SHA-256 hashing failed");
    offset += count;
  }
  std::array<unsigned char, 32> digest{};
  require(BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0) >= 0,
          "Cannot finish SHA-256 hash");
  return hex(digest.data(), digest.size());
}

std::string fetchHttps(const std::string &url, std::size_t maxBytes) {
  require(maxBytes > 0 && maxBytes <= MaxAsset, "Invalid HTTPS download limit");
  approvedUrl(url);
  const auto deadline = Clock::now() + std::chrono::seconds(120);
  const auto agent = wide(std::string("YilaiCodexSwitcher/") + updates::Version + " updater");
  Internet session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
  winCheck(WinHttpSetTimeouts(session.value, 5000, 10000, 10000, 15000) != FALSE,
           "Cannot set HTTPS timeouts");
  DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
  winCheck(WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS,
                            &protocols, sizeof(protocols)) != FALSE, "Cannot require modern TLS");
  auto current = url;
  for (unsigned redirects = 0; redirects <= 5; ++redirects) {
    require(Clock::now() < deadline, "HTTPS total deadline exceeded");
    const auto parsed = approvedUrl(current);
    Internet connection(WinHttpConnect(session.value, parsed.host.c_str(), 443, 0));
    Internet request(WinHttpOpenRequest(connection.value, L"GET", parsed.object.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    winCheck(WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE,
                              &disable, sizeof(disable)) != FALSE, "Cannot restrict HTTPS request");
    DWORD headersLimit = 65536;
    winCheck(WinHttpSetOption(request.value, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE,
                              &headersLimit, sizeof(headersLimit)) != FALSE, "Cannot bound HTTPS headers");
    auto state = std::make_shared<AsyncState>();
    winCheck(WinHttpSetStatusCallback(request.value, completion,
        WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) != WINHTTP_INVALID_STATUS_CALLBACK,
        "Cannot register HTTPS completion callback");
    auto binding = std::make_unique<AsyncContext>(AsyncContext{state});
    const auto context = reinterpret_cast<DWORD_PTR>(binding.get());
    winCheck(WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE,
        const_cast<DWORD_PTR *>(&context), sizeof(context)) != FALSE, "Cannot bind HTTPS callback");
    // HANDLE_CLOSING owns this binding, including outstanding read buffers after a timeout.
    binding.release();
    state->reset();
    winCheck(WinHttpSendRequest(request.value, L"Accept: application/octet-stream, application/json\r\nAccept-Encoding: identity\r\n",
        DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, context) != FALSE, "Cannot send HTTPS request");
    waitHttps(state, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, deadline, 15000);
    state->reset();
    winCheck(WinHttpReceiveResponse(request.value, nullptr) != FALSE, "Cannot receive HTTPS response");
    waitHttps(state, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, deadline, 15000);
    DWORD status = 0, bytes = sizeof(status);
    winCheck(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX) != FALSE,
        "Cannot read HTTPS status");
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      require(redirects < 5, "Too many HTTPS redirects");
      const auto location = header(request.value, WINHTTP_QUERY_LOCATION);
      require(location.has_value(), "HTTPS redirect has no location");
      current = redirectUrl(current, *location);
      continue;
    }
    require(status == 200, "Update server did not return HTTP 200");
    const auto encoding = header(request.value, WINHTTP_QUERY_CONTENT_ENCODING);
    require(!encoding || *encoding == L"identity", "Unexpected HTTPS content encoding");
    const auto length = header(request.value, WINHTTP_QUERY_CONTENT_LENGTH);
    const auto size = length ? std::optional<std::size_t>(responseSize(*length, maxBytes)) : std::nullopt;
    std::string out;
    if (size) out.reserve(*size);
    for (;;) {
      state->reset();
      winCheck(WinHttpReadData(request.value, state->buffer.data(), DWORD(state->buffer.size()), nullptr) != FALSE,
               "Cannot read HTTPS data");
      waitHttps(state, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, deadline, 15000);
      const auto count = state->count.load();
      require(count <= state->buffer.size() && count <= maxBytes - out.size(),
              "HTTPS response exceeds the size limit");
      if (!count) break;
      out.append(state->buffer.data(), count);
    }
    require(!size || *size == out.size(), "HTTPS content length does not match downloaded bytes");
    return out;
  }
  throw std::runtime_error("Too many HTTPS redirects");
}

UpdateInfo checkSoftwareUpdate() {
  const auto value = updates::release(fetchHttps(updates::ReleaseURL, 2 * 1024 * 1024), AssetName);
  UpdateInfo info;
  info.available = value.at("available").get<bool>();
  info.version = wide(value.at("version").get<std::string>());
  info.notes = wide(value.at("notes").get<std::string>());
  if (info.available) {
    info.url = value.at("url").get<std::string>();
    info.sha256 = value.at("sha256").get<std::string>();
    info.size = value.at("size").get<std::size_t>();
    approvedUrl(info.url);
  }
  return info;
}
ModelDownload downloadModels() {
  const auto manifest = updates::channel(fetchHttps(updates::ChannelURL, 65536));
  auto data = fetchHttps(manifest.at("url").get<std::string>(), MaxCatalog);
  require(sha256(data) == manifest.at("sha256").get<std::string>(), "Model catalog SHA-256 mismatch");
  updates::catalog(data);
  return {std::move(data), manifest.at("revision").get<long long>()};
}

void installSoftwareUpdate(const UpdateInfo &info) {
  require(info.available && info.size > 0 && info.size <= MaxAsset &&
              std::regex_match(info.sha256, std::regex("[a-f0-9]{64}")) &&
              updates::version(utf8(info.version)) > updates::version(updates::Version),
          "No valid newer software update was requested");
  approvedUrl(info.url);
  const auto target = executable();
  safePath(target, false);
  require(!(GetFileAttributesW(target.c_str()) & FILE_ATTRIBUTE_READONLY),
          "The application executable is read-only");
  const auto module = GetModuleHandleW(nullptr);
  const auto resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_UPDATE), RT_RCDATA);
  require(resource != nullptr, "The embedded update helper is unavailable");
  const auto loaded = LoadResource(module, resource);
  const auto bytes = SizeofResource(module, resource);
  const auto *scriptData = static_cast<const char *>(LockResource(loaded));
  require(scriptData && bytes > 0 && bytes <= 256 * 1024, "Invalid embedded update helper");
  const std::string script(scriptData, bytes);
  require(script.find('\0') == std::string::npos, "Invalid embedded update helper encoding");
  const auto powershell = systemDirectory() / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
  safePath(powershell, false);
  const auto id = token();
  const auto stage = absolutePath(target.parent_path() / wide(".yilai-update-" + id));
  makeDirectory(stage);
  struct Cleanup {
    fs::path path; bool helperOwns = false;
    ~Cleanup() { if (!helperOwns) cleanStage(path); }
  } cleanup{stage};
  const auto probe = stage / L"permission-check";
  writeNew(probe, "permission-check");
  const auto moved = stage / L"permission-ok";
  winCheck(MoveFileExW(probe.c_str(), moved.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE &&
               DeleteFileW(moved.c_str()), "Update directory does not allow file replacement");
  const auto result = resultDirectory(true) / L"update-result.json";
  safePath(result, false, true);
  const auto fresh = checkSoftwareUpdate();
  require(fresh.available && fresh.version == info.version && fresh.url == info.url &&
              fresh.sha256 == info.sha256 && fresh.size == info.size,
          "Release metadata changed; check for updates again");
  const auto data = fetchHttps(info.url, info.size);
  require(data.size() == info.size && sha256(data) == info.sha256,
          "Software update size or SHA-256 mismatch");
  validatePE(data);
  const auto source = stage / wide(AssetName);
  writeNew(source, data);
  validateVersion(source, utf8(info.version));
  candidateSelfTest(source, stage);
  require(sha256(readFile(source, info.size)) == info.sha256,
          "Software update changed during self-test");
  const auto helper = stage / L"update.ps1";
  writeNew(helper, script);
  const auto backup = absolutePath(fs::path(target.wstring() + wide(".old-" + id)));
  safePath(backup, false, true);
  require(GetFileAttributesW(backup.c_str()) == INVALID_FILE_ATTRIBUTES,
          "Update backup name is already in use");
  FILETIME created{}, exited{}, kernel{}, user{};
  winCheck(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE,
           "Cannot identify the running application process");
  const auto started = (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
  const auto eventName = wide("Local\\YilaiCodexSwitcher-update-" + id);
  Handle ready(CreateEventW(nullptr, TRUE, FALSE, eventName.c_str()));
  winCheck(ready.value != nullptr, "Cannot create update helper handshake");
  require(GetLastError() != ERROR_ALREADY_EXISTS, "Update handshake name is already in use");
  Json parameters = {{"Script", utf8(helper.wstring())}, {"Source", utf8(source.wstring())},
      {"Target", utf8(target.wstring())}, {"Backup", utf8(backup.wstring())},
      {"Stage", utf8(stage.wstring())}, {"Sha256", info.sha256}, {"Size", info.size},
      {"Version", utf8(info.version)}, {"ProcessId", GetCurrentProcessId()},
      {"ProcessStarted", started}, {"Token", id}, {"ReadyEvent", utf8(eventName)}};
  // All variable data is JSON inside base64; filesystem paths never become script syntax.
  const auto commandText = wide(
      "$ErrorActionPreference='Stop';$p=ConvertFrom-Json ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('" +
      base64(parameters.dump()) + "'))); & ([string]$p.Script) -Source ([string]$p.Source) "
      "-Target ([string]$p.Target) -Backup ([string]$p.Backup) -Stage ([string]$p.Stage) "
      "-Sha256 ([string]$p.Sha256) -Size ([long]$p.Size) -Version ([string]$p.Version) "
      "-ProcessId ([int]$p.ProcessId) -ProcessStarted ([long]$p.ProcessStarted) "
      "-Token ([string]$p.Token) -ReadyEvent ([string]$p.ReadyEvent);exit $LASTEXITCODE");
  static_assert(sizeof(wchar_t) == 2, "PowerShell requires UTF-16LE");
  const auto encoded = base64(std::string(reinterpret_cast<const char *>(commandText.data()),
                                        commandText.size() * sizeof(wchar_t)));
  auto command = quoteArgument(powershell.wstring()) +
      L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " + wide(encoded);
  require(command.size() < 32767, "Update helper command is too long");
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION process{};
  winCheck(CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE,
      CREATE_NO_WINDOW, nullptr, target.parent_path().c_str(), &startup, &process) != FALSE,
      "Cannot start the update helper");
  Handle child(process.hProcess), thread(process.hThread);
  const HANDLE waits[] = {ready.value, child.value};
  const auto waited = WaitForMultipleObjects(2, waits, FALSE, 20000);
  if (waited != WAIT_OBJECT_0) {
    TerminateProcess(child.value, 1);
    WaitForSingleObject(child.value, 3000);
    throw std::runtime_error("The update helper did not become ready; the application was not replaced");
  }
  cleanup.helperOwns = true;
}

std::wstring previousUpdateResult() {
  try {
    const auto path = resultDirectory(false) / L"update-result.json";
    safePath(path, false, true);
    Handle file(CreateFileW(path.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) return {};
    const auto value = Json::parse(readHandle(file.value, 16384));
    if (!value.is_object() || value.value("schema_version", 0) != 1 ||
        value.value("product", std::string()) != "YilaiCodexSwitcher" ||
        !std::regex_match(value.value("token", std::string()), std::regex("[a-f0-9]{32}")) ||
        !samePath(wide(value.at("target").get<std::string>()), executable().wstring())) return {};
    const auto version = value.at("version").get<std::string>();
    updates::version(version);
    const auto status = value.at("status").get<std::string>();
    std::wstring message;
    if (status == "success") message = L"\u8f6f\u4ef6\u5df2\u66f4\u65b0\u81f3 " + wide(version) + L"\u3002";
    else if (status == "rolled_back")
      message = L"\u8f6f\u4ef6\u66f4\u65b0\u5931\u8d25\uff0c\u5df2\u6062\u590d\u5e76\u91cd\u65b0\u542f\u52a8\u65e7\u7248\u672c\u3002";
    else if (status == "failed")
      message = L"\u8f6f\u4ef6\u66f4\u65b0\u672a\u5b8c\u6210\uff0c\u539f\u7a0b\u5e8f\u5df2\u4fdd\u7559\u3002";
    else if (status == "rollback_failed")
      message = L"\u8f6f\u4ef6\u66f4\u65b0\u5931\u8d25\uff0c\u81ea\u52a8\u6062\u590d\u672a\u5b8c\u6210\u3002\u65e7\u7248\u5907\u4efd\u4fdd\u7559\u5728\u7a0b\u5e8f\u65c1\uff0c\u8bf7\u624b\u52a8\u6062\u590d\u3002";
    else return {};
    FILE_DISPOSITION_INFO disposition{TRUE};
    // Delete the validated, opened receipt, rather than a possibly swapped pathname.
    SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition));
    return message;
  } catch (...) { return {}; }
}

bool updateSelfTest(std::wstring &error) {
  try {
    require(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "Empty SHA-256 vector failed");
    require(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 vector failed");
    require(sha256(std::string("a\0b", 3)) ==
            "59b271ae1bbcb1d31d41929817f4b16fb439eb4f31520b5ad1d5ce98920a7138",
            "Binary SHA-256 vector failed");
    approvedUrl(updates::ReleaseURL); approvedUrl(updates::ChannelURL);
    approvedUrl("https://github.com/kingduoyu/yilai-codex-switcher/releases/download/v3.4.1/YilaiCodexSwitcher.exe");
    approvedUrl("https://release-assets.githubusercontent.com/github-production-release-asset/1/asset?sig=test");
    approvedUrl("https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/" +
                std::string(40, 'a') + "/model-catalog.json");
    for (const auto *url : {"http://github.com/kingduoyu/yilai-codex-switcher/releases/latest",
        "https://api.github.com.evil.invalid/repos/kingduoyu/yilai-codex-switcher/releases/latest",
        "https://user@api.github.com/repos/kingduoyu/yilai-codex-switcher/releases/latest",
        "https://raw.githubusercontent.com/other/repo/main/model-channel.json",
        "https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/main/../model-channel.json",
        "https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/main%2fmodel-channel.json",
        "https://api.github.com:444/repos/kingduoyu/yilai-codex-switcher/releases/latest",
        "https://objects.githubusercontent.com/other/asset"}) {
      bool rejected = false;
      try { approvedUrl(url); } catch (...) { rejected = true; }
      require(rejected, "Unsafe HTTPS URL was accepted");
    }
    require(responseSize(L"100", 100) == 100 && responseSize(L"0", 1) == 0,
            "HTTPS response size validation failed");
    for (const auto *length : {L"101", L"999999999999999999999999", L"-1", L"1,1", L""}) {
      bool rejected = false;
      try { responseSize(length, 100); } catch (...) { rejected = true; }
      require(rejected, "Unsafe HTTPS response size was accepted");
    }
    require(base64("") == "" && base64("a") == "YQ==" && base64("ab") == "YWI=" &&
                base64("abc") == "YWJj" && base64(std::string("\0\xff", 2)) == "AP8=",
            "Update command encoding failed");
    require(quoteArgument(L"a b\\") == L"\"a b\\\\\"" &&
                quoteArgument(L"a\"b") == L"\"a\\\"b\"",
            "Update command argument quoting failed");
    bool rejected = false;
    try { validatePE("not an executable"); } catch (...) { rejected = true; }
    require(rejected, "Invalid Windows executable was accepted");
    error.clear();
    return true;
  } catch (const std::exception &failure) {
    error = wide(failure.what());
    return false;
  }
}
} // namespace app
