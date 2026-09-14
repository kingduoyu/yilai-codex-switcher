#ifndef YILAI_RUNTIME_PROBE_H
#define YILAI_RUNTIME_PROBE_H
#include <filesystem>
#include <string>
#include <vector>
namespace yilai_sources {
// Finds an already-installed runtime without launching, downloading or installing.
std::filesystem::path locate_runtime();
// Bootstrap disposable storage with empty history, then read all contexts in
// one runtime session. Both phases share a single deadline.
std::vector<std::string> probe_configs(const std::filesystem::path &runtime,
                         const std::filesystem::path &home,
                         const std::vector<std::filesystem::path> &contexts);
// Read-only config/read RPC result, including layers. Throws sanitized categories.
// Reads the specified home; isolates runtime databases in temporary storage.
// Terminates the child before removing its temporary storage.
std::string probe_config(const std::filesystem::path &runtime,
                         const std::filesystem::path &home,
                         const std::filesystem::path &cwd);
}
#endif
