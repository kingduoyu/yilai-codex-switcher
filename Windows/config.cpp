#include "config.hpp"
#include "model_catalog.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kProviderId = "yilai";
constexpr const char* kMarker = "# Managed by Yilai Codex Switcher";
constexpr const char* kOfficialModel = "gpt-5.6-terra";



std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

[[noreturn]] void fail(const std::wstring& message) {
    throw std::runtime_error(wideToUtf8(message));
}

std::wstring errorText(const std::exception& error) {
    return utf8ToWide(error.what());
}

bool pathExists(const fs::path& path) {
    std::error_code error;
    return fs::exists(path, error);
}

std::string readBytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail(L"无法读取文件：" + path.wstring());
    std::ostringstream output;
    output << stream.rdbuf();
    return output.str();
}

void writeAtomic(const fs::path& path, const std::string& data) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) fail(L"无法创建目录：" + path.parent_path().wstring());

    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path temporary = path.parent_path() / (L"." + path.filename().wstring() + L"." + std::to_wstring(stamp) + L".tmp");
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) fail(L"无法创建临时文件：" + temporary.wstring());
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream) {
            fs::remove(temporary, error);
            fail(L"无法写入文件：" + path.wstring());
        }
    }

    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        fs::remove(temporary, error);
        fail(L"无法替换文件（错误 " + std::to_wstring(code) + L"）：" + path.wstring());
    }
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring trim(const std::wstring& value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> splitLines(const std::string& value) {
    std::vector<std::string> lines;
    std::string normalized;
    normalized.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\r') {
            if (i + 1 < value.size() && value[i + 1] == '\n') continue;
            normalized.push_back('\n');
        } else {
            normalized.push_back(value[i]);
        }
    }
    while (!normalized.empty() && normalized.back() == '\n') normalized.pop_back();
    std::istringstream stream(normalized);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);
    return lines;
}

bool isTableHeader(const std::string& line) {
    const std::string value = trim(line);
    return value.size() >= 2 && value.front() == '[' && value.back() == ']';
}

bool isSetting(const std::string& line, const std::string& key) {
    const std::string value = trim(line);
    if (value.rfind(key, 0) != 0) return false;
    return trim(value.substr(key.size())).rfind("=", 0) == 0;
}

void setTopLevel(std::vector<std::string>& lines, const std::string& key, const std::string& value) {
    size_t firstTable = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (isTableHeader(lines[i])) {
            firstTable = i;
            break;
        }
    }
    for (size_t i = 0; i < firstTable; ++i) {
        if (isSetting(lines[i], key)) {
            lines[i] = key + " = " + value;
            return;
        }
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(firstTable), key + " = " + value);
}

void removeTopLevel(std::vector<std::string>& lines, const std::string& key) {
    size_t firstTable = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (isTableHeader(lines[i])) {
            firstTable = i;
            break;
        }
    }
    for (size_t i = firstTable; i-- > 0;) {
        if (isSetting(lines[i], key)) lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

bool isModelProviderHeader(const std::string& line) {
    const std::string value = trim(line);
    return value.rfind("[model_providers.", 0) == 0 && value.size() > 18 && value.back() == ']';
}

void removeAllModelProviders(std::vector<std::string>& lines) {
    while (true) {
        auto startIt = std::find_if(lines.begin(), lines.end(), isModelProviderHeader);
        if (startIt == lines.end()) return;
        size_t start = static_cast<size_t>(std::distance(lines.begin(), startIt));
        size_t end = start + 1;
        while (end < lines.size() && !isTableHeader(lines[end])) ++end;
        if (start > 0 && trim(lines[start - 1]) == kMarker) --start;
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(start), lines.begin() + static_cast<std::ptrdiff_t>(end));
    }
}

void removeManagedProvider(std::vector<std::string>& lines) {
    const std::string header = std::string("[model_providers.") + kProviderId + "]";
    while (true) {
        auto startIt = std::find_if(lines.begin(), lines.end(), [&](const std::string& line) { return trim(line) == header; });
        if (startIt == lines.end()) return;
        size_t start = static_cast<size_t>(std::distance(lines.begin(), startIt));
        size_t end = start + 1;
        while (end < lines.size() && !isTableHeader(lines[end])) ++end;
        if (start > 0 && trim(lines[start - 1]) == kMarker) --start;
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(start), lines.begin() + static_cast<std::ptrdiff_t>(end));
    }
}

std::string escapeToml(std::string value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '"') result.push_back('\\');
        result.push_back(character);
    }
    return result;
}

std::string buildYilaiConfig(const std::string& existing, const std::string& key, const fs::path& catalog) {
    const std::string newline = existing.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    auto lines = splitLines(existing);
    removeManagedProvider(lines);
    setTopLevel(lines, "cli_auth_credentials_store", "\"file\"");
    setTopLevel(lines, "model_provider", "\"yilai\"");
    setTopLevel(lines, "model", "\"gpt-5.6-sol\"");
    removeTopLevel(lines, "model_catalog_json");
    setTopLevel(lines, "model_catalog_json", "\"" + escapeToml(wideToUtf8(catalog.wstring())) + "\"");
    while (!lines.empty() && trim(lines.back()).empty()) lines.pop_back();
    if (!lines.empty()) lines.emplace_back();
    lines.emplace_back(kMarker);
    lines.emplace_back("[model_providers.yilai]");
    lines.emplace_back("name = \"易来 API\"");
    lines.emplace_back("base_url = \"https://api.yilai-ai.com\"");
    lines.emplace_back("wire_api = \"responses\"");
    lines.emplace_back("requires_openai_auth = false");
    lines.emplace_back("http_headers = { \"x-openai-actor-authorization\" = \"local-image-extension\" }");
    lines.emplace_back("experimental_bearer_token = \"" + escapeToml(key) + "\"");

    std::ostringstream output;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) output << newline;
        output << lines[i];
    }
    output << newline;
    return output.str();
}

std::string buildOfficialConfig(const std::string& existing) {
    const std::string newline = existing.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    auto lines = splitLines(existing);
    removeAllModelProviders(lines);
    setTopLevel(lines, "cli_auth_credentials_store", "\"file\"");
    removeTopLevel(lines, "model_provider");
    removeTopLevel(lines, "model_catalog_json");
    setTopLevel(lines, "model", std::string("\"") + kOfficialModel + "\"");
    while (!lines.empty() && trim(lines.back()).empty()) lines.pop_back();

    std::ostringstream output;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) output << newline;
        output << lines[i];
    }
    output << newline;
    return output.str();
}

void applyConfiguration(const CodexPaths& paths, const std::string& config, bool writeCatalog) {
    struct Snapshot { fs::path path; bool existed; std::string bytes; };
    std::vector<Snapshot> snapshots;
    std::vector<size_t> changed;
    // Rollback data exists only in memory, never in a persistent login backup.
    const std::vector<fs::path> targets = {paths.config, paths.modelCatalog, paths.auth,
                                         paths.disabledAuth, paths.manifest, paths.backupConfig};
    for (const auto& target : targets) {
        const bool existed = fs::exists(target);
        if (existed && !fs::is_regular_file(target)) fail(L"预期为普通文件：" + target.wstring());
        snapshots.push_back({target, existed, existed ? readBytes(target) : std::string()});
    }
    changed.reserve(targets.size());
    try {
        writeAtomic(paths.config, config);
        changed.push_back(0);
        if (writeCatalog) {
            writeAtomic(paths.modelCatalog, kYilaiModelCatalog);
            changed.push_back(1);
        }
        for (size_t i = 2; i < targets.size(); ++i) {
            if (fs::remove(targets[i])) changed.push_back(i);
        }
        if (fs::exists(paths.auth) || fs::exists(paths.disabledAuth)) {
            fail(L"认证文件被重新创建，请完全退出 Codex 和 CC-Switch 后重试");
        }
    } catch (...) {
        bool rollbackFailed = false;
        for (auto it = changed.rbegin(); it != changed.rend(); ++it) {
            const auto& snapshot = snapshots[*it];
            try {
                if (snapshot.existed) writeAtomic(snapshot.path, snapshot.bytes);
                else fs::remove(snapshot.path);
            } catch (...) { rollbackFailed = true; }
        }
        if (rollbackFailed) fail(L"切换失败且回滚不完整，请检查文件权限后重试");
        throw;
    }
    // Remove only the empty legacy directory, never unrelated files inside it.
    std::error_code ignored;
    fs::remove(paths.backup, ignored);
}

void require(bool condition, const std::wstring& message) {
    if (!condition) fail(message);
}

} // namespace

CodexPaths pathsFor(const fs::path& codex) {
    const fs::path backup = codex / L"yilai-switcher-backup";
    return {codex, codex / L"config.toml", codex / L"auth.json", codex / L"auth.json.yilai-disabled",
            backup, backup / L"manifest.json", backup / L"config.toml", codex / L"yilai-model-catalog.json"};
}

CodexPaths currentPaths() {
    PWSTR profile = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &profile)) || !profile) {
        fail(L"无法确定当前用户目录");
    }
    fs::path root(profile);
    CoTaskMemFree(profile);
    return pathsFor(root / L".codex");
}

CodexMode getMode(const CodexPaths& paths) {
    if (!pathExists(paths.config)) return CodexMode::NotConfigured;
    for (const auto& line : splitLines(readBytes(paths.config))) {
        if (isTableHeader(line)) break;
        if (isSetting(line, "model_provider")) {
            const size_t separator = line.find('=');
            const std::string value = separator == std::string::npos ? "" : trim(line.substr(separator + 1));
            if (value == "\"yilai\"" || value == "'yilai'") return CodexMode::Yilai;
            if (value == "\"openai\"" || value == "'openai'") return CodexMode::Official;
            return CodexMode::Other;
        }
    }
    return CodexMode::Official;
}

void switchToYilai(const std::wstring& rawKey, const CodexPaths& paths) {
    const std::wstring keyWide = trim(rawKey);
    if (keyWide.empty()) fail(L"API Key 不能为空");
    if (std::any_of(keyWide.begin(), keyWide.end(), [](wchar_t c) { return c < 32 || c == 127; })) {
        fail(L"API Key 包含无效字符");
    }
    std::error_code error;
    fs::create_directories(paths.codex, error);
    if (error) fail(L"无法创建 .codex 目录");
    const std::string existing = pathExists(paths.config) ? readBytes(paths.config) : std::string();
    applyConfiguration(paths, buildYilaiConfig(existing, wideToUtf8(keyWide), paths.modelCatalog), true);
}

void switchToOfficial(const CodexPaths& paths) {
    std::error_code error;
    fs::create_directories(paths.codex, error);
    if (error) fail(L"无法创建 .codex 目录");

    const std::string existing = pathExists(paths.config) ? readBytes(paths.config) : std::string();
    applyConfiguration(paths, buildOfficialConfig(existing), false);
}

bool runSelfTest(std::wstring& error) {
    try {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const fs::path root = fs::temp_directory_path() / (L"YilaiCodexSwitcher-cpp-" + std::to_wstring(stamp));
        fs::create_directories(root);
        struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path, e); } } cleanup{root};

        const auto paths = pathsFor(root);
        const std::string originalConfig = "model_provider = \"custom\"\r\nmodel = \"gpt-old\"\r\n\r\n[model_providers.custom]\r\nname = \"Original\"\r\n\r\n[plugins.\"browser@openai-bundled\"]\r\nenabled = true\r\n";
        const std::string originalAuth = "{\"auth_mode\":\"chatgpt\"}";
        writeAtomic(paths.config, originalConfig);
        writeAtomic(paths.auth, originalAuth);
        require(getMode(paths) == CodexMode::Other, L"自测失败：第三方模式识别");
        switchToYilai(L"sk-test-key", paths);
        const std::string yilai = readBytes(paths.config);
        require(getMode(paths) == CodexMode::Yilai, L"自测失败：模式识别");
        require(yilai.find("requires_openai_auth = false") != std::string::npos, L"自测失败：认证设置");
        require(yilai.find("local-image-extension") != std::string::npos, L"自测失败：生图请求头");
        require(yilai.find("sk-test-key") != std::string::npos, L"自测失败：API Key");
        require(yilai.find("model_catalog_json = \"" + escapeToml(wideToUtf8(paths.modelCatalog.wstring())) + "\"") != std::string::npos,
                L"自测失败：模型目录路径");
        require(readBytes(paths.modelCatalog) == kYilaiModelCatalog, L"自测失败：固定模型目录");
        require(yilai.find("[plugins.\"browser@openai-bundled\"]") != std::string::npos, L"自测失败：插件配置保留");
        require(!pathExists(paths.auth) && !pathExists(paths.disabledAuth) && !pathExists(paths.backup), L"自测失败：不应保留登录备份");
        switchToYilai(L"sk-new-key", paths);
        require(readBytes(paths.config).find("sk-new-key") != std::string::npos, L"自测失败：重复切换");
        switchToOfficial(paths);
        const std::string official = readBytes(paths.config);
        require(getMode(paths) == CodexMode::Official, L"自测失败：官方模式识别");
        require(official.find("model = \"gpt-5.6-terra\"") != std::string::npos, L"自测失败：官方模型");
        require(official.find("model_provider") == std::string::npos, L"自测失败：第三方 provider 仍被选中");
        require(official.find("model_catalog_json") == std::string::npos, L"自测失败：官方模式仍限制模型目录");
        require(official.find("[model_providers.") == std::string::npos, L"自测失败：第三方 provider 定义未清理");
        require(official.find("sk-new-key") == std::string::npos, L"自测失败：易来 Key 未清理");
        require(official.find("[plugins.\"browser@openai-bundled\"]") != std::string::npos, L"自测失败：官方切换未保留通用配置");
        require(!pathExists(paths.auth) && !pathExists(paths.disabledAuth), L"自测失败：官方应重新登录");

        const auto newPaths = pathsFor(root / L"new-user");
        switchToYilai(L"sk-new-user", newPaths);
        require(pathExists(newPaths.config) && !pathExists(newPaths.auth), L"自测失败：新用户切换");
        switchToOfficial(newPaths);
        require(pathExists(newPaths.config) && getMode(newPaths) == CodexMode::Official,
                L"自测失败：新用户切换官方");
        require(readBytes(newPaths.config).find("model = \"gpt-5.6-terra\"") != std::string::npos,
                L"自测失败：新用户官方模型");
        require(!pathExists(newPaths.auth), L"自测失败：新用户不应生成登录凭据");

        const auto directPaths = pathsFor(root / L"direct-official");
        fs::create_directories(directPaths.codex);
        writeAtomic(directPaths.config, originalConfig);
        switchToOfficial(directPaths);
        require(getMode(directPaths) == CodexMode::Official,
                L"自测失败：无备份时不能直接切换官方");

        for (const bool officialMode : {false, true}) {
            const auto conflictPaths = pathsFor(root / (officialMode ? L"conflict-official" : L"conflict-yilai"));
            fs::create_directories(conflictPaths.codex);
            writeAtomic(conflictPaths.config, originalConfig);
            writeAtomic(conflictPaths.auth, originalAuth);
            writeAtomic(conflictPaths.disabledAuth, "old auth");
            writeAtomic(conflictPaths.manifest, "invalid legacy manifest");
            writeAtomic(conflictPaths.backupConfig, originalConfig);
            writeAtomic(conflictPaths.backup / L"unrelated.txt", "keep");
            if (officialMode) switchToOfficial(conflictPaths);
            else switchToYilai(L"sk-test-conflict", conflictPaths);
            require(getMode(conflictPaths) == (officialMode ? CodexMode::Official : CodexMode::Yilai) &&
                    !pathExists(conflictPaths.auth) && !pathExists(conflictPaths.disabledAuth) &&
                    !pathExists(conflictPaths.manifest) && !pathExists(conflictPaths.backupConfig) &&
                    readBytes(conflictPaths.backup / L"unrelated.txt") == "keep",
                    L"自测失败：旧备份冲突清理");
        }

        const auto rollbackPaths = pathsFor(root / L"rollback");
        fs::create_directories(rollbackPaths.codex);
        writeAtomic(rollbackPaths.config, originalConfig);
        writeAtomic(rollbackPaths.modelCatalog, "previous catalog");
        writeAtomic(rollbackPaths.auth, originalAuth);
        writeAtomic(rollbackPaths.disabledAuth, "locked old auth");
        // Deny deletion of the second auth file after the first has been removed.
        HANDLE locked = CreateFileW(rollbackPaths.disabledAuth.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(locked != INVALID_HANDLE_VALUE, L"自测失败：无法锁定测试文件");
        bool rollbackPassed = true;
        for (const bool officialMode : {false, true}) {
            bool failed = false;
            try {
                if (officialMode) switchToOfficial(rollbackPaths);
                else switchToYilai(L"sk-after-failure", rollbackPaths);
            } catch (...) { failed = true; }
            rollbackPassed = rollbackPassed && failed && readBytes(rollbackPaths.config) == originalConfig &&
                readBytes(rollbackPaths.modelCatalog) == "previous catalog" &&
                readBytes(rollbackPaths.auth) == originalAuth &&
                readBytes(rollbackPaths.disabledAuth) == "locked old auth";
        }
        CloseHandle(locked);
        require(rollbackPassed, L"自测失败：文件清理失败应回滚配置、目录与登录");

        const auto upgradePaths = pathsFor(root / L"old-user");
        fs::create_directories(upgradePaths.codex);
        writeAtomic(upgradePaths.config, originalConfig);
        writeAtomic(upgradePaths.auth, originalAuth);
        switchToYilai(L"sk-old-key", upgradePaths);
        writeAtomic(upgradePaths.manifest, "{\"AuthExisted\":true}");
        writeAtomic(upgradePaths.backupConfig, originalConfig);
        writeAtomic(upgradePaths.disabledAuth, originalAuth);
        auto oldLines = splitLines(readBytes(upgradePaths.config));
        removeTopLevel(oldLines, "model_catalog_json");
        std::string oldConfig = "model_catalog_json = \"old-catalog.json\"\nmodel_catalog_json = \"stale-catalog.json\"\n";
        for (const auto& line : oldLines) oldConfig += line + "\n";
        writeAtomic(upgradePaths.config, oldConfig);
        writeAtomic(upgradePaths.modelCatalog, "old catalog containing luna");
        writeAtomic(upgradePaths.codex / L"models_cache.json", "old cache containing luna");
        switchToYilai(L"sk-upgraded-key", upgradePaths);
        const std::string upgraded = readBytes(upgradePaths.config);
        const size_t catalogSetting = upgraded.find("model_catalog_json");
        require(catalogSetting != std::string::npos && upgraded.find("model_catalog_json", catalogSetting + 1) == std::string::npos &&
                upgraded.find("old-catalog.json") == std::string::npos && upgraded.find("sk-upgraded-key") != std::string::npos &&
                readBytes(upgradePaths.modelCatalog) == kYilaiModelCatalog && !pathExists(upgradePaths.manifest) &&
                !pathExists(upgradePaths.disabledAuth) &&
                readBytes(upgradePaths.codex / L"models_cache.json") == "old cache containing luna",
                L"自测失败：旧用户目录覆盖与旧备份清理");
        switchToOfficial(upgradePaths);
        require(readBytes(upgradePaths.config).find("model_catalog_json") == std::string::npos &&
                !pathExists(upgradePaths.auth), L"自测失败：旧用户升级后切回官方");
        return true;
    } catch (const std::exception& exception) {
        error = errorText(exception);
        return false;
    }
}
