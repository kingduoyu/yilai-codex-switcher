#pragma once
#include "vendor/json.hpp"
#include <array>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>

namespace updates {
using Json = nlohmann::json;
inline constexpr char Version[] = "3.4.1";
inline constexpr char ReleaseURL[] = "https://api.github.com/repos/kingduoyu/yilai-codex-switcher/releases/latest";
inline constexpr char ChannelURL[] = "https://raw.githubusercontent.com/kingduoyu/yilai-codex-switcher/main/model-channel.json";
inline void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
inline std::array<int, 3> version(std::string value) {
  if (!value.empty() && value[0] == 'v') value.erase(0, 1);
  std::smatch match;
  require(std::regex_match(value, match, std::regex("([0-9]{1,4})\\.([0-9]{1,4})\\.([0-9]{1,4})")), "Invalid release version");
  return {std::stoi(match[1]), std::stoi(match[2]), std::stoi(match[3])};
}
inline Json catalog(const std::string &text) {
  require(text.size() <= 4 * 1024 * 1024 && text.find('\0') == std::string::npos, "Invalid catalog size/encoding");
  auto value = Json::parse(text);
  require(value.is_object() && value.contains("models") && value["models"].is_array() &&
          !value["models"].empty() && value["models"].size() <= 100, "Invalid model catalog");
  std::set<std::string> ids;
  for (const auto &model : value["models"]) {
    const auto id = model.at("slug").get<std::string>();
    require(std::regex_match(id, std::regex("[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}")) && ids.insert(id).second,
            "Invalid or duplicate model id");
    require(model.at("display_name").is_string() && model.at("context_window").is_number_integer() &&
            model.at("context_window").get<long long>() > 0 && model.at("supported_reasoning_levels").is_array() &&
            model.at("model_messages").is_object(), "Incomplete model metadata");
  }
  return value;
}
inline void additive(const Json &before, const Json &after) {
  for (const auto &old : before.at("models")) {
    bool found = false;
    for (const auto &model : after.at("models")) if (model.at("slug") == old.at("slug")) found = true;
    require(found, "Catalog would remove an existing model; update refused");
  }
}
inline Json release(const std::string &text, const std::string &assetName) {
  auto value = Json::parse(text);
  require(!value.at("draft").get<bool>() && !value.at("prerelease").get<bool>(), "Release is not stable");
  auto tag = value.at("tag_name").get<std::string>();
  version(tag);
  Json result = {{"version", tag}, {"notes", value.value("body", std::string())}, {"available", version(tag) > version(Version)}};
  if (!result["available"].get<bool>()) return result;
  for (const auto &asset : value.at("assets")) if (asset.at("name") == assetName) {
    const auto digest = asset.at("digest").get<std::string>();
    require(std::regex_match(digest, std::regex("sha256:[a-f0-9]{64}")), "Release SHA-256 is missing");
    auto url = asset.at("browser_download_url").get<std::string>();
    require(url == "https://github.com/kingduoyu/yilai-codex-switcher/releases/download/" + tag + "/" + assetName,
            "Unexpected release URL");
    auto size = asset.at("size").get<long long>();
    require(size > 0 && size <= 100 * 1024 * 1024, "Invalid release asset size");
    result["url"] = url; result["sha256"] = digest.substr(7); result["size"] = size;
    return result;
  }
  throw std::runtime_error("Release is missing the automatic-update package");
}
inline Json channel(const std::string &text) {
  auto value = Json::parse(text);
  require(value.at("schema_version") == 1 && value.at("revision").get<long long>() > 0, "Unsupported model channel");
  require(std::regex_match(value.at("sha256").get<std::string>(), std::regex("[a-f0-9]{64}")), "Invalid catalog SHA-256");
  require(std::regex_match(value.at("url").get<std::string>(), std::regex("https://raw\\.githubusercontent\\.com/kingduoyu/yilai-codex-switcher/[a-f0-9]{40}/model-catalog\\.json")), "Catalog must use an immutable repository URL");
  return value;
}
}
