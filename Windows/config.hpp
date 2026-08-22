#pragma once

#include <filesystem>
#include <string>

enum class CodexMode {
    NotConfigured,
    OfficialOrOther,
    Yilai,
};

struct CodexPaths {
    std::filesystem::path codex;
    std::filesystem::path config;
    std::filesystem::path auth;
    std::filesystem::path disabledAuth;
    std::filesystem::path backup;
    std::filesystem::path manifest;
    std::filesystem::path backupConfig;
};

CodexPaths currentPaths();
CodexPaths pathsFor(const std::filesystem::path& codex);
CodexMode getMode(const CodexPaths& paths);
void switchToYilai(const std::wstring& rawKey, const CodexPaths& paths);
void switchToOfficial(const CodexPaths& paths);
bool runSelfTest(std::wstring& error);

