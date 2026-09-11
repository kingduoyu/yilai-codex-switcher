#include "../Windows/Platform.h"
#include "../Sources/ConfigSources/runtime_probe.h"
#include <iostream>
#include <windows.h>
int wmain(int argc, wchar_t **argv) {
  if (argc != 3 && argc != 4)
    return 2;
  try {
    std::wstring name(argv[1]);
    if (name != L"configure" && name != L"cleanup") return 2;
    auto action = name == L"configure" ? app::Action::Configure : app::Action::Cleanup;
    app::run(action, std::filesystem::absolute(argv[2]),
             L"sk-isolated-test-only", false, argc == 4 ? (std::wstring(argv[3]) == L"--auto-runtime" ? yilai_sources::locate_runtime() : std::filesystem::path(argv[3])) : std::filesystem::path{});
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
