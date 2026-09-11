#include "../Windows/Platform.h"
#include <iostream>
#include <windows.h>
int wmain(int argc, wchar_t **argv) {
  if (argc != 3)
    return 2;
  try {
    std::wstring name(argv[1]);
    auto action = name == L"images"      ? app::Action::Images
                  : name == L"configure" ? app::Action::Configure
                  : name == L"official"  ? app::Action::Official
                  : name == L"sync"      ? app::Action::Sync
                  : name == L"undo"      ? app::Action::Undo
                                         : app::Action::Cleanup;
    app::run(action, std::filesystem::absolute(argv[2]),
             L"sk-isolated-test-only", false);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
