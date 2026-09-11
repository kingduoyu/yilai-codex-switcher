#pragma once
#include <filesystem>
#include <string>
#include <functional>
namespace app {
enum class Action { Configure, Cleanup };
std::filesystem::path home();
std::wstring mode(const std::filesystem::path &home);
std::wstring run(Action action, const std::filesystem::path &home,
                 const std::wstring &key = L"", bool requireClosed = true,
                 const std::filesystem::path &runtimeOverride = {},
                 std::function<void(const std::wstring &)> progress = {});
bool selfTest(std::wstring &error);
} // namespace app
