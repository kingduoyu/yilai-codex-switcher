#include "../Windows/Platform.h"
#include "../Sources/ConfigSources/runtime_probe.h"
#include <iostream>
#include "HistorySync.h"
#include "ConfigRewrite.h"
#include <windows.h>
int wmain(int argc, wchar_t **argv) {
  if (argc == 2 && std::wstring(argv[1]) == L"app-self-test") {
    std::wstring error;
    const bool ok = app::selfTest(error);
    if (!ok) std::wcerr << error << '\n';
    return ok ? 0 : 1;
  }
  if (argc == 2 && std::wstring(argv[1]) == L"history-self-test") {
    char *error = nullptr;
    int ok = yilai_history_self_test(&error);
    if (error) { std::cerr << error << '\n'; yilai_config_free(error); }
    return ok ? 0 : 1;
  }
  if (argc != 3 && argc != 4)
    return 2;
  try {
    std::wstring name(argv[1]);
    if (name != L"configure" && name != L"cleanup" && name != L"official" && name != L"unify") return 2;
    auto action = name == L"configure" ? app::Action::Configure : name == L"official" ? app::Action::Official : name == L"unify" ? app::Action::UnifyHistory : app::Action::Cleanup;
    app::run(action, std::filesystem::absolute(argv[2]),
             L"sk-isolated-test-only", false, argc == 4 ? (std::wstring(argv[3]) == L"--auto-runtime" ? yilai_sources::locate_runtime() : std::filesystem::path(argv[3])) : std::filesystem::path{});
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
