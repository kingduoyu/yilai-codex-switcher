#pragma once

#include <cstddef>
#include <string>

namespace app {
struct UpdateInfo {
  bool available = false;
  std::wstring version, notes;
  std::string url, sha256;
  std::size_t size = 0;
};

UpdateInfo checkSoftwareUpdate();
void installSoftwareUpdate(const UpdateInfo &info);
std::string fetchHttps(const std::string &url, std::size_t maxBytes);
std::string sha256(const std::string &data);

struct ModelDownload {
  std::string data;
  long long revision;
};
ModelDownload downloadModels();
std::wstring previousUpdateResult();

// Offline checks; does not download, launch an executable, or change files.
bool updateSelfTest(std::wstring &error);
} // namespace app
