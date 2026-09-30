#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <string>
#ifdef FIXTURE_OLD
constexpr auto Kind = "old";
#else
constexpr auto Kind = "new";
#endif
int fixtureMain(int argc, wchar_t **argv) {
  if (argc > 1 && std::wstring(argv[1]) == L"--self-test") return 0;
  if (argc > 1 && std::wstring(argv[1]) == L"--hold") { Sleep(120000); return 0; }
  wchar_t executable[32768]{};
  GetModuleFileNameW(nullptr, executable, 32768);
  auto path = std::filesystem::path(executable).parent_path() / L"fixture-started.txt";
  std::ofstream proof(path, std::ios::trunc);
  proof << Kind;
  proof.close();
#ifdef FIXTURE_FAIL_START
  return 1;
#else
  Sleep(6000);
#endif
  return 0;
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  auto result = fixtureMain(argc, argv);
  LocalFree(argv);
  return result;
}
