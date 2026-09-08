#include "config.hpp"
#include "model_catalog.hpp"
#include "ConfigRewrite.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {




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
    if (stream.bad()) fail(L"无法完整读取文件：" + path.wstring());
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



std::wstring trim(const std::wstring& value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
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

std::string buildConfig(const std::string& existing, const std::string& key,
                        const fs::path& catalog, bool official) {
    if (existing.find('\0') != std::string::npos) fail(L"config.toml 包含无效空字符，未修改文件。");
    char* error = nullptr;
    const auto path = wideToUtf8(catalog.wstring());
    using Buffer = std::unique_ptr<char, decltype(&yilai_config_free)>;
    Buffer output(yilai_rewrite_config(existing.c_str(), key.c_str(), path.c_str(), official, &error), yilai_config_free);
    Buffer diagnostic(error, yilai_config_free);
    if (!output) fail(L"无法生成配置：" + utf8ToWide(diagnostic ? diagnostic.get() : "Invalid configuration."));
    return output.get();
}

void recycleFile(const fs::path& path) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) fail(L"无法初始化回收站操作");
    struct ComScope { ~ComScope() { CoUninitialize(); } } scope;
    Microsoft::WRL::ComPtr<IFileOperation> operation;
    Microsoft::WRL::ComPtr<IShellItem> item;
    auto check = [&](HRESULT result) {
        if (FAILED(result)) fail(L"无法移入回收站，已停止切换：" + path.wstring());
    };
    check(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(operation.GetAddressOf())));
    // Require recycling explicitly; never retry with a permanent-delete API.
    check(operation->SetOperationFlags(FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE | FOF_NO_UI));
    check(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.GetAddressOf())));
    check(operation->DeleteItem(item.Get(), nullptr));
    check(operation->PerformOperations());
    BOOL aborted = FALSE;
    check(operation->GetAnyOperationsAborted(&aborted));
    if (aborted || fs::exists(path)) fail(L"文件未移入回收站，已停止切换：" + path.wstring());
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
            if (fs::exists(targets[i])) {
                recycleFile(targets[i]);
                changed.push_back(i);
            }
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
    switch (yilai_config_mode(readBytes(paths.config).c_str())) {
    case 0: return CodexMode::Official;
    case 1: return CodexMode::Yilai;
    case 2: return CodexMode::Other;
    default: fail(L"config.toml 格式无效，请修复后重试。");
    }
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
    applyConfiguration(paths, buildConfig(existing, wideToUtf8(keyWide), paths.modelCatalog, false), true);
}

void switchToOfficial(const CodexPaths& paths) {
    std::error_code error;
    fs::create_directories(paths.codex, error);
    if (error) fail(L"无法创建 .codex 目录");

    const std::string existing = pathExists(paths.config) ? readBytes(paths.config) : std::string();
    applyConfiguration(paths, buildConfig(existing, "", paths.modelCatalog, true), false);
}

bool runSelfTest(std::wstring& error) {
    try {
        char* coreError = nullptr;
        const bool corePassed = yilai_config_self_test(&coreError) != 0;
        const std::string diagnostic = coreError ? coreError : "Structured configuration self-test failed.";
        yilai_config_free(coreError);
        require(corePassed, utf8ToWide(diagnostic));
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
        const std::string oldConfig = "model_catalog_json = \"old-catalog.json\"\n" + originalConfig;
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
