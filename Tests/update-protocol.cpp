#include "../Sources/Shared/update_protocol.hpp"
#include "../Sources/ConfigRewrite/include/ConfigRewrite.h"
#include <fstream>
#include <iostream>

using updates::Json;
template <typename F> void rejected(F action) {
  bool threw = false;
  try { action(); } catch (const std::exception &) { threw = true; }
  updates::require(threw, "Invalid update was accepted");
}
int main(int argc, char **argv) {
  try {
    updates::require(argc == 2, "Pass the model catalog fixture");
    std::ifstream input(argv[1], std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    const auto catalog = updates::catalog(bytes);
    auto duplicate = catalog;
    duplicate["models"].push_back(duplicate["models"][0]);
    rejected([&] { updates::catalog(duplicate.dump()); });
    auto removal = catalog;
    removal["models"].erase(0);
    rejected([&] { updates::additive(catalog, removal); });
    updates::require(updates::version("3.10.0") > updates::version("v3.9.9"), "Version ordering failed");
    rejected([] { updates::version("v4.0.0-rc1"); });
    Json asset = {{"name", "YilaiCodexSwitcher.exe"}, {"digest", "sha256:" + std::string(64, 'a')},
                  {"size", 100}, {"browser_download_url", "https://github.com/kingduoyu/yilai-codex-switcher/releases/download/v9.0.0/YilaiCodexSwitcher.exe"}};
    Json release = {{"tag_name", "v9.0.0"}, {"draft", false}, {"prerelease", false}, {"assets", Json::array({asset})}};
    updates::require(updates::release(release.dump(), "YilaiCodexSwitcher.exe")["available"].get<bool>(), "New release missed");
    release["assets"][0]["browser_download_url"] = "https://example.com/package.exe";
    rejected([&] { updates::release(release.dump(), "YilaiCodexSwitcher.exe"); });
    release["assets"] = Json::array({asset});
    release["assets"][0]["digest"] = nullptr;
    rejected([&] { updates::release(release.dump(), "YilaiCodexSwitcher.exe"); });
    release["assets"] = Json::array({asset}); release["prerelease"] = true;
    rejected([&] { updates::release(release.dump(), "YilaiCodexSwitcher.exe"); });
    Json channel = {{"schema_version", 1}, {"revision", 1}, {"sha256", std::string(64, 'a')},
                    {"url", "https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/" + std::string(40, 'a') + "/model-catalog.json"}};
    updates::channel(channel.dump());
    channel["url"] = "https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/main/model-catalog.json";
    rejected([&] { updates::channel(channel.dump()); });
    auto future = catalog;
    auto model = catalog["models"][0]; model["slug"] = "synthetic-future-model";
    future["models"].push_back(model);
    for (const char *config : {"model='synthetic-future-model'\n", "profile='work'\n[profiles.work]\nmodel='synthetic-future-model'\n"}) {
      char *error = nullptr;
      char *rewritten = yilai_configure_catalog_data(config, "catalog.json", future.dump().c_str(), &error);
      updates::require(rewritten && !error, "Future model rewrite failed");
      const std::string result(rewritten); yilai_config_free(rewritten);
      updates::require(result.find("synthetic-future-model") != std::string::npos, "Future model was reset");
    }
    std::cout << "PASS: release identity/digest, version ordering, immutable channel, catalog rejection and future model preservation\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
