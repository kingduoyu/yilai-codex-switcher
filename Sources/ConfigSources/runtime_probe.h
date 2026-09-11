#ifndef YILAI_RUNTIME_PROBE_H
#define YILAI_RUNTIME_PROBE_H
#include <filesystem>
#include <string>
namespace yilai_sources {
// Finds an already-installed runtime without launching, downloading or installing.
std::filesystem::path locate_runtime();
// Read-only config/read RPC result, including layers. Throws sanitized categories.
// Isolates CODEX_HOME and CODEX_SQLITE_HOME to home; terminates the child on exit.
std::string probe_config(const std::filesystem::path &runtime,
                         const std::filesystem::path &home,
                         const std::filesystem::path &cwd);
}
#endif
