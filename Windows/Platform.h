#pragma once
#include <filesystem>
#include <string>
namespace app {
enum class Action { Images, Configure, Sync, Undo, Cleanup };
std::filesystem::path home();
std::wstring mode(const std::filesystem::path& home);
std::wstring run(Action action, const std::filesystem::path& home, const std::wstring& key = L"", bool requireClosed = true);
bool selfTest(std::wstring& error);
}
