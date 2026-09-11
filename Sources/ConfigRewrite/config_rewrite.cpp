#include "ConfigRewrite.h"
#include "vendor/toml.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
toml::table &table_at(toml::table &parent, const char *key) {
  if (!parent.contains(key))
    parent.insert(key, toml::table{});
  auto *table = parent[key].as_table();
  if (!table)
    throw std::runtime_error(std::string("Expected a TOML table: ") + key);
  return *table;
}
toml::table *active_profile(toml::table &root) {
  if (!root.contains("profile"))
    return nullptr;
  auto name = root["profile"].value<std::string>();
  if (!name)
    throw std::runtime_error("The selected profile must be a string.");
  auto *profiles = root["profiles"].as_table();
  auto *profile = profiles ? (*profiles)[*name].as_table() : nullptr;
  if (!profile)
    throw std::runtime_error("The selected profile does not exist.");
  return profile;
}
std::string provider_id(toml::table &root) {
  auto *profile = active_profile(root);
  auto &scope =
      profile && profile->contains("model_provider") ? *profile : root;
  if (!scope.contains("model_provider"))
    return "openai";
  auto id = scope["model_provider"].value<std::string>();
  if (!id || id->empty())
    throw std::runtime_error("Invalid model_provider.");
  return *id;
}
toml::table *selected_provider(toml::table &root) {
  auto id = provider_id(root);
  auto *providers = root["model_providers"].as_table();
  auto *provider = providers ? (*providers)[id].as_table() : nullptr;
  if (!provider && id != "openai")
    throw std::runtime_error(
        "Selected provider is missing. Reapply the connection in CCS first.");
  return provider;
}
bool is_yilai(const toml::table *provider) {
  if (!provider)
    return false;
  auto url = (*provider)["base_url"].value_or(std::string());
  std::transform(url.begin(), url.end(), url.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  const std::string host = "https://api.yilai-ai.com";
  return url == host || url.rfind(host + "/", 0) == 0;
}
void enhance(toml::table &root) {
  auto *provider = selected_provider(root);
  // These are the only changes made by the default operation.
  table_at(root, "features").insert_or_assign("image_generation", true);
  if (auto *profile = active_profile(root))
    table_at(*profile, "features").insert_or_assign("image_generation", true);
  if (provider)
    table_at(*provider, "http_headers")
        .insert_or_assign("x-openai-actor-authorization",
                          "local-image-extension");
}
void remove_owned_catalog(toml::table &scope) {
  auto value = scope["model_catalog_json"].value_or(std::string());
  auto offset = value.find_last_of("/\\");
  if (value.substr(offset == std::string::npos ? 0 : offset + 1) ==
      "yilai-model-catalog.json")
    scope.erase("model_catalog_json");
}
void clear_active_connection_constraints(toml::table &root) {
  // The selected profile inherits root constraints; inactive profile tables
  // keep their own explicit login requirements and endpoint overrides.
  for (auto *scope : {&root, active_profile(root)})
    if (scope)
      for (const char *field :
           {"forced_login_method", "forced_chatgpt_workspace_id",
            "openai_base_url", "chatgpt_base_url"})
        scope->erase(field);
}
toml::table clear_connection_overrides(const char *text) {
  auto root = toml::parse(text);
  toml::table *profile = nullptr;
  if (root.contains("profile")) {
    auto name = root["profile"].value<std::string>();
    if (!name)
      throw std::runtime_error("The selected profile must be a string.");
    // A layered file may select a profile declared in another file. Only
    // rewrite the selected profile when its table is present in this input.
    auto *profiles = root["profiles"].as_table();
    profile = profiles ? (*profiles)[*name].as_table() : nullptr;
  }
  auto *providers = root["model_providers"].as_table();
  auto *previous = providers ? (*providers)["custom"].as_table() : nullptr;
  auto *profiles = root["profiles"].as_table();
  bool has_inactive_reference = false;
  if (previous && profiles)
    for (auto &[name, node] : *profiles) {
      auto *other = node.as_table();
      if (other && other != profile && (*other)["model_provider"] == "custom")
        has_inactive_reference = true;
    }
  if (has_inactive_reference) {
    std::string saved = "yilai-sync-previous-custom";
    for (int suffix = 2;; ++suffix) {
      bool occupied = providers->contains(saved);
      for (auto &[name, node] : *profiles) {
        auto *other = node.as_table();
        auto *local = other ? (*other)["model_providers"].as_table() : nullptr;
        if (local && local->contains(saved))
          occupied = true;
      }
      if (!occupied)
        break;
      saved = "yilai-sync-previous-custom-" + std::to_string(suffix);
    }
    providers->insert_or_assign(saved, *previous);
    for (auto &[name, node] : *profiles) {
      auto *other = node.as_table();
      if (!other || other == profile || (*other)["model_provider"] != "custom")
        continue;
      other->insert_or_assign("model_provider", saved);
      // Preserve any inline override of the old root provider under its alias.
      if (auto *local = (*other)["model_providers"].as_table())
        if (auto *custom = (*local)["custom"].as_table())
          local->insert_or_assign(saved, *custom);
    }
  }
  for (auto *scope : {&root, profile}) {
    if (!scope)
      continue;
    for (const char *field :
         {"model_provider", "forced_login_method", "forced_chatgpt_workspace_id",
          "openai_base_url", "chatgpt_base_url"})
      scope->erase(field);
    if (auto *features = (*scope)["features"].as_table()) {
      const auto removed = features->erase("image_generation");
      if (removed && features->empty())
        scope->erase("features");
    }
    if (auto *providers = (*scope)["model_providers"].as_table())
      providers->erase("custom");
  }
  return root;
}
void install_custom(toml::table &root, toml::table provider) {
  auto *profile = active_profile(root);
  const auto previous_root =
      root["model_provider"].value_or(std::string("openai"));
  auto &providers = table_at(root, "model_providers");
  auto *profiles = root["profiles"].as_table();
  bool referenced_elsewhere = profile && previous_root == "custom";
  if (profiles)
    for (auto &[name, node] : *profiles) {
      auto *other = node.as_table();
      if (!other || other == profile)
        continue;
      if ((*other)["model_provider"] == "custom" ||
          (!profile && !other->contains("model_provider") &&
           previous_root == "custom"))
        referenced_elsewhere = true;
    }
  std::string previous_custom = "custom";
  auto *previous = providers["custom"].as_table();
  // Archive only a changed route still used outside the selected scope. An
  // unreferenced old connection is replaced, not retained with its old key.
  if (previous && *previous != provider && referenced_elsewhere) {
    previous_custom = "yilai-sync-previous-custom";
    for (int i = 2; providers.contains(previous_custom); ++i)
      previous_custom = "yilai-sync-previous-custom-" + std::to_string(i);
    providers.insert_or_assign(previous_custom, *previous);
    if (profile && previous_root == "custom")
      root.insert_or_assign("model_provider", previous_custom);
  }
  if (profiles) {
    for (auto &[name, node] : *profiles) {
      auto *other = node.as_table();
      if (!other || other == profile)
        continue;
      if ((*other)["model_provider"] == "custom" && previous_custom != "custom")
        other->insert_or_assign("model_provider", previous_custom);
      else if (!profile && !other->contains("model_provider") &&
               (previous_root != "custom" || previous_custom != "custom"))
        // Pin inherited routes only when the root's route actually changes.
        other->insert_or_assign("model_provider", previous_root == "custom"
                                                      ? previous_custom
                                                      : previous_root);
    }
  }
  providers.insert_or_assign("custom", std::move(provider));
  (profile ? *profile : root).insert_or_assign("model_provider", "custom");
}
toml::table rewrite(const char *text, const char *key, int action) {
  auto root = toml::parse(text);
  auto *profile = active_profile(root);
  if (action == YILAI_ENHANCE) {
    if (root.empty())
      throw std::runtime_error("Configure a connection in CCS first, or use "
                               "Configure Yilai with an API key.");
    enhance(root);
  } else if (action == YILAI_CONFIGURE) {
    const std::string token(key);
    if (token.empty() ||
        std::any_of(token.begin(), token.end(),
                    [](unsigned char c) { return c < 32 || c == 127; }))
      throw std::runtime_error(
          "A non-empty API key without control characters is required.");
    // Explicit connection configuration, not the default image operation.
    // CCS uses custom for its third-party session bucket. Model/catalog/auth
    // files stay intact.
    toml::table provider{{"name", "易来 API"},
                         {"base_url", "https://api.yilai-ai.com"},
                         {"wire_api", "responses"},
                         {"requires_openai_auth", false},
                         {"experimental_bearer_token", token}};
    table_at(provider, "http_headers")
        .insert_or_assign("x-openai-actor-authorization",
                          "local-image-extension");
    install_custom(root, std::move(provider));
    clear_active_connection_constraints(root);
    enhance(root);
  } else if (action == YILAI_CLEANUP) {
    auto *provider = selected_provider(root);
    if (!is_yilai(provider) ||
        (*provider)["experimental_bearer_token"]
            .value_or(std::string())
            .empty() ||
        (*provider)["requires_openai_auth"].value_or(true))
      throw std::runtime_error(
          "Cleanup requires a direct Yilai connection with its own bearer key. "
          "Use Configure Yilai first; official/CCS credentials are protected.");
    for (auto *scope : {&root, profile})
      if (scope) {
        for (const char *field :
             {"forced_login_method", "forced_chatgpt_workspace_id",
              "openai_base_url"})
          scope->erase(field);
        scope->insert_or_assign("cli_auth_credentials_store", "file");
        remove_owned_catalog(*scope);
      }
  } else if (action == YILAI_OFFICIAL) {
    // Match CCS's native official provider while retaining the shared history
    // bucket. Never copy the previous provider's endpoint, token or headers.
    toml::table official{{"name", "OpenAI"},
                         {"wire_api", "responses"},
                         {"requires_openai_auth", true},
                         {"supports_websockets", true}};
    auto *providers = root["model_providers"].as_table();
    auto *current = providers ? (*providers)["custom"].as_table() : nullptr;
    if (provider_id(root) != "custom" || !current || *current != official)
      install_custom(root, std::move(official));
    clear_active_connection_constraints(root);
    for (auto *scope : {&root, profile})
      if (scope)
        remove_owned_catalog(*scope);
  } else if (action == YILAI_UNIFY_HISTORY) {
    auto id = provider_id(root);
    auto *current = selected_provider(root);
    if (id != "custom") {
      toml::table unified = current
                                ? *current
                                : toml::table{{"name", "OpenAI"},
                                              {"wire_api", "responses"},
                                              {"requires_openai_auth", true},
                                              {"supports_websockets", true}};
      install_custom(root, std::move(unified));
    }
  } else
    throw std::runtime_error("Unknown configuration operation.");
  return root;
}
std::string format(const toml::table &root) {
  std::ostringstream out;
  out << toml::toml_formatter{root, toml::format_flags::allow_unicode_strings}
      << '\n';
  return out.str();
}
char *copy_string(const std::string &text) {
  auto *result = static_cast<char *>(std::malloc(text.size() + 1));
  if (result)
    std::memcpy(result, text.c_str(), text.size() + 1);
  return result;
}
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void self_test() {
  const char *ccs = R"toml(
profile = "work"
model_provider = "custom"
model = "gpt-6-astra"
model_catalog_json = "cc-switch-model-catalog.json"
cli_auth_credentials_store = "keyring"
forced_login_method = "chatgpt"
forced_chatgpt_workspace_id = "keep"
approval_policy = "never"
[features]
image_generation = false
[model_providers.custom]
name = "Other CCS provider"
base_url = "https://other.invalid/v1"
wire_api = "responses"
requires_openai_auth = false
experimental_bearer_token = "synthetic-original"
[model_providers.custom.http_headers]
X-Keep = "keep"
[model_providers.spare]
name = "Unrelated"
[profiles.work]
model = "gpt-6-astra"
[profiles.work.features]
image_generation = false
[mcp_servers.sample]
command = "keep"
[plugins.sample]
enabled = true
)toml";
  auto before = toml::parse(ccs);
  auto expected = before;
  table_at(expected, "features").insert_or_assign("image_generation", true);
  table_at(*active_profile(expected), "features")
      .insert_or_assign("image_generation", true);
  table_at(*selected_provider(expected), "http_headers")
      .insert_or_assign("x-openai-actor-authorization",
                        "local-image-extension");
  auto enabled = rewrite(ccs, "", YILAI_ENHANCE);
  check(enabled == expected,
        "Image enhancement modified unrelated CCS values.");
  check(rewrite(format(enabled).c_str(), "", YILAI_ENHANCE) == enabled,
        "Enhancement is not idempotent.");
  auto connected = rewrite(ccs, "new-key", YILAI_CONFIGURE);
  check(connected["model"] == before["model"] &&
            connected["model_catalog_json"] == before["model_catalog_json"],
        "Configuration changed model/catalog.");
  check(!connected["forced_login_method"] &&
            !connected["forced_chatgpt_workspace_id"] &&
            connected["cli_auth_credentials_store"] ==
                before["cli_auth_credentials_store"],
        "Connection switching retained forced login or changed the credentials "
        "store.");
  check(connected["model_providers"]["spare"] ==
            before["model_providers"]["spare"],
        "Inactive provider changed.");
  auto standalone = rewrite("model = 'gpt-6-astra'\n", "first-key", YILAI_CONFIGURE);
  for (int i = 0; i < 8; ++i)
    check(rewrite(format(standalone).c_str(), "first-key", YILAI_CONFIGURE) ==
              standalone,
          "Repeated API switching must be idempotent.");
  auto new_key = rewrite(format(standalone).c_str(), "second-key", YILAI_CONFIGURE);
  check(new_key["model_providers"].as_table()->size() == 1 &&
            new_key["model_providers"]["custom"]["experimental_bearer_token"] ==
                "second-key" &&
            format(new_key).find("first-key") == std::string::npos,
        "An unreferenced old API connection or key was archived.");
  auto roundtrip = new_key;
  for (int i = 0; i < 8; ++i) {
    roundtrip = rewrite(format(roundtrip).c_str(), "", YILAI_OFFICIAL);
    check(roundtrip["model_providers"].as_table()->size() == 1 &&
              format(roundtrip).find("second-key") == std::string::npos,
          "Official switching archived an unreferenced API key.");
    roundtrip = rewrite(format(roundtrip).c_str(), "second-key", YILAI_CONFIGURE);
    check(roundtrip == new_key,
          "API/official round trips accumulated unused providers.");
  }
  auto identical_shared = standalone;
  table_at(table_at(identical_shared, "profiles"), "personal")
      .insert_or_assign("model_provider", "custom");
  table_at(table_at(identical_shared, "profiles"), "inherited")
      .insert_or_assign("model", "gpt-6-astra");
  check(rewrite(format(identical_shared).c_str(), "first-key", YILAI_CONFIGURE) ==
            identical_shared,
        "An unchanged shared provider was unnecessarily archived or pinned.");
  identical_shared.insert_or_assign("profile", "work");
  auto &same_work = table_at(table_at(identical_shared, "profiles"), "work");
  same_work.insert_or_assign("model_provider", "custom");
  table_at(same_work, "features").insert_or_assign("image_generation", true);
  check(rewrite(format(identical_shared).c_str(), "first-key", YILAI_CONFIGURE) ==
            identical_shared,
        "An unchanged active profile redirected the root or another profile.");
  auto changed_shared =
      rewrite(format(identical_shared).c_str(), "second-key", YILAI_CONFIGURE);
  check(changed_shared["model_providers"].as_table()->size() == 2 &&
            changed_shared["model_provider"] == "yilai-sync-previous-custom" &&
            changed_shared["profiles"]["personal"]["model_provider"] ==
                "yilai-sync-previous-custom" &&
            !changed_shared["profiles"]["inherited"]["model_provider"] &&
            changed_shared["model_providers"]["yilai-sync-previous-custom"] ==
                identical_shared["model_providers"]["custom"],
        "Changing a shared key did not preserve inactive root/profile routes.");
  check(rewrite(format(changed_shared).c_str(), "second-key", YILAI_CONFIGURE) ==
            changed_shared,
        "Repeated profile switching kept archiving its unreferenced route.");
  auto shared_roundtrip = changed_shared;
  for (int i = 0; i < 4; ++i) {
    shared_roundtrip = rewrite(format(shared_roundtrip).c_str(), "", YILAI_OFFICIAL);
    shared_roundtrip =
        rewrite(format(shared_roundtrip).c_str(), "second-key", YILAI_CONFIGURE);
    check(shared_roundtrip == changed_shared,
          "Shared-profile round trips lost a saved route or added providers.");
  }
  auto retained_legacy = standalone;
  table_at(retained_legacy, "model_providers")
      .insert_or_assign("yilai-sync-previous-custom",
                        toml::table{{"name", "Keep existing saved route"}});
  auto retained_changed =
      rewrite(format(retained_legacy).c_str(), "second-key", YILAI_CONFIGURE);
  check(retained_changed["model_providers"].as_table()->size() == 2 &&
            retained_changed["model_providers"]["yilai-sync-previous-custom"] ==
                retained_legacy["model_providers"]["yilai-sync-previous-custom"],
        "Switching removed or replaced an existing historical provider.");
  auto cleared = rewrite(format(connected).c_str(), "", YILAI_CLEANUP);
  check(!cleared.contains("forced_login_method") &&
            cleared["model_catalog_json"] == before["model_catalog_json"],
        "Cleanup did not preserve CCS catalog.");
  auto owned = connected;
  owned.insert_or_assign("model_catalog_json",
                         "C:\\codex\\yilai-model-catalog.json");
  check(!rewrite(format(owned).c_str(), "", YILAI_CLEANUP)
             .contains("model_catalog_json"),
        "Owned stale catalog pointer not cleared.");
  for (auto action : {YILAI_CLEANUP}) {
    bool rejected = false;
    try {
      rewrite(ccs, "", action);
    } catch (...) {
      rejected = true;
    }
    check(rejected, "Other provider credentials must not be cleaned.");
  }
  auto repaired = rewrite(ccs, "", YILAI_UNIFY_HISTORY);
  check(repaired == before, "Legacy alias does not use current route.");

  const char *multiple_profiles = R"toml(
profile='work'
model_provider='custom'
[model_providers.custom]
name='Personal service'
base_url='https://personal.invalid/v1'
[model_providers.yilai]
name='Work service'
base_url='https://api.yilai-ai.com'
[profiles.work]
model_provider='yilai'
[profiles.personal]
model_provider='custom'
[profiles.inherited]
model='unchanged'
)toml";
  auto unified_profiles = rewrite(multiple_profiles, "", YILAI_UNIFY_HISTORY);
  check(unified_profiles["profiles"]["work"]["model_provider"] == "custom",
        "Active profile was not unified.");
  check(unified_profiles["model_provider"] == "yilai-sync-previous-custom" &&
            unified_profiles["profiles"]["personal"]["model_provider"] ==
                "yilai-sync-previous-custom" &&
            unified_profiles["model_providers"]["yilai-sync-previous-custom"]
                            ["base_url"] == "https://personal.invalid/v1",
        "History sync redirected an inactive profile or the root connection.");

  auto configured_profiles =
      rewrite(multiple_profiles, "new-key", YILAI_CONFIGURE);
  check(configured_profiles["profiles"]["work"]["model_provider"] == "custom" &&
            configured_profiles["model_providers"]["custom"]
                               ["experimental_bearer_token"] == "new-key" &&
            configured_profiles["model_provider"] ==
                "yilai-sync-previous-custom" &&
            configured_profiles["profiles"]["personal"]["model_provider"] ==
                "yilai-sync-previous-custom" &&
            !configured_profiles["profiles"]["inherited"]["model_provider"] &&
            configured_profiles["model_providers"]["yilai-sync-previous-custom"]
                               ["base_url"] == "https://personal.invalid/v1",
        "Explicit connection configuration redirected an inactive profile.");
  auto shared_current = toml::parse(multiple_profiles);
  table_at(table_at(shared_current, "profiles"), "work")
      .insert_or_assign("model_provider", "custom");
  table_at(shared_current, "model_providers")
      .insert_or_assign("yilai-sync-previous-custom",
                        toml::table{{"name", "Existing saved connection"}});
  auto reconfigured =
      rewrite(format(shared_current).c_str(), "new-key", YILAI_CONFIGURE);
  check(reconfigured["profiles"]["work"]["model_provider"] == "custom" &&
            reconfigured["model_provider"] == "yilai-sync-previous-custom-2" &&
            reconfigured["profiles"]["personal"]["model_provider"] ==
                "yilai-sync-previous-custom-2" &&
            reconfigured["model_providers"]["yilai-sync-previous-custom"]
                        ["name"] == "Existing saved connection" &&
            reconfigured["model_providers"]["yilai-sync-previous-custom-2"] ==
                shared_current["model_providers"]["custom"],
        "Replacing the active custom provider changed a shared route or "
        "overwrote a saved connection.");
  auto root_active = toml::parse(multiple_profiles);
  root_active.erase("profile");
  for (auto action : {YILAI_CONFIGURE, YILAI_UNIFY_HISTORY, YILAI_OFFICIAL}) {
    // With no selected profile, profiles that inherit the root must keep their
    // old backend.
    root_active.insert_or_assign("model_provider", "yilai");
    auto switched = rewrite(format(root_active).c_str(), "new-key", action);
    check(switched["model_provider"] == "custom" &&
              switched["profiles"]["inherited"]["model_provider"] == "yilai" &&
              switched["profiles"]["personal"]["model_provider"] ==
                  "yilai-sync-previous-custom" &&
              switched["profiles"]["work"]["model_provider"] == "yilai",
          "Changing the root redirected explicit or inherited inactive profile "
          "routes.");
    root_active.erase("model_provider");
    auto from_official =
        rewrite(format(root_active).c_str(), "new-key", action);
    check(
        from_official["profiles"]["inherited"]["model_provider"] == "openai",
        "An inactive profile lost its inherited built-in official connection.");
  }
  root_active.insert_or_assign("model_provider", "custom");
  auto replaced_root =
      rewrite(format(root_active).c_str(), "new-key", YILAI_CONFIGURE);
  check(replaced_root["profiles"]["inherited"]["model_provider"] ==
                "yilai-sync-previous-custom" &&
            replaced_root["model_providers"]["yilai-sync-previous-custom"] ==
                root_active["model_providers"]["custom"],
        "An inactive profile inherited new credentials after replacing root "
        "custom.");
  auto official = rewrite("model = 'gpt-6-astra'\n", "", YILAI_UNIFY_HISTORY);
  check(official["model_provider"] == "custom" &&
            official["model_providers"]["custom"]["requires_openai_auth"] ==
                true &&
            !official["model_providers"]["custom"]["experimental_bearer_token"],
        "Official alias carries third-party credentials.");
  auto restored_official =
      rewrite(format(connected).c_str(), "", YILAI_OFFICIAL);
  const toml::table official_provider{{"name", "OpenAI"},
                                      {"wire_api", "responses"},
                                      {"requires_openai_auth", true},
                                      {"supports_websockets", true}};
  check(*selected_provider(restored_official) == official_provider &&
            restored_official["profiles"]["work"]["model_provider"] ==
                "custom" &&
            restored_official["model"] == connected["model"] &&
            restored_official["model_catalog_json"] ==
                connected["model_catalog_json"] &&
            restored_official["cli_auth_credentials_store"] ==
                connected["cli_auth_credentials_store"] &&
            restored_official["mcp_servers"] == connected["mcp_servers"] &&
            restored_official["plugins"] == connected["plugins"] &&
            !restored_official["forced_login_method"] &&
            !restored_official["forced_chatgpt_workspace_id"],
        "Official switching retained third-party routing or changed user "
        "settings.");
  check(rewrite(format(restored_official).c_str(), "", YILAI_OFFICIAL) ==
            restored_official,
        "Repeated official switching must not create more saved providers.");
  auto official_profiles = rewrite(multiple_profiles, "", YILAI_OFFICIAL);
  check(
      official_profiles["profiles"]["work"]["model_provider"] == "custom" &&
          *selected_provider(official_profiles) == official_provider &&
          official_profiles["profiles"]["personal"]["model_provider"] ==
              "yilai-sync-previous-custom" &&
          official_profiles["model_provider"] == "yilai-sync-previous-custom" &&
          !official_profiles["profiles"]["inherited"]["model_provider"] &&
          official_profiles["model_providers"]["yilai-sync-previous-custom"] ==
              toml::parse(multiple_profiles)["model_providers"]["custom"],
      "Official switching redirected inactive profiles.");
  auto restricted = connected;
  restricted.insert_or_assign("openai_base_url", "https://third-party.invalid");
  restricted.insert_or_assign("chatgpt_base_url",
                              "https://third-party.invalid");
  restricted.insert_or_assign("model_catalog_json",
                              "C:\\codex\\yilai-model-catalog.json");
  auto *restricted_profile = active_profile(restricted);
  restricted_profile->insert_or_assign("forced_login_method", "api");
  restricted_profile->insert_or_assign("forced_chatgpt_workspace_id",
                                       "old-workspace");
  restricted_profile->insert_or_assign("openai_base_url",
                                       "https://third-party.invalid");
  restricted_profile->insert_or_assign("chatgpt_base_url",
                                       "https://third-party.invalid");
  restricted_profile->insert_or_assign("model_catalog_json", "ccs-keep.json");
  auto unrestricted = rewrite(format(restricted).c_str(), "", YILAI_OFFICIAL);
  for (auto *scope : {&unrestricted, active_profile(unrestricted)})
    check(!scope->contains("forced_login_method") &&
              !scope->contains("forced_chatgpt_workspace_id") &&
              !scope->contains("openai_base_url") &&
              !scope->contains("chatgpt_base_url"),
          "Official login inherited stale third-party constraints.");
  check(!unrestricted["model_catalog_json"] &&
            unrestricted["profiles"]["work"]["model_catalog_json"] ==
                "ccs-keep.json",
        "Official switching must remove only the old owned model catalog.");
  auto api_unrestricted =
      rewrite(format(restricted).c_str(), "new-key", YILAI_CONFIGURE);
  for (auto *scope : {&api_unrestricted, active_profile(api_unrestricted)})
    check(!scope->contains("forced_login_method") &&
              !scope->contains("forced_chatgpt_workspace_id") &&
              !scope->contains("openai_base_url") &&
              !scope->contains("chatgpt_base_url"),
          "API switching inherited stale official login constraints.");
  check(api_unrestricted["model_catalog_json"] ==
                restricted["model_catalog_json"] &&
            api_unrestricted["profiles"]["work"]["model_catalog_json"] ==
                "ccs-keep.json",
        "API switching unexpectedly replaced the model catalog.");
  auto fresh_official = rewrite("", "", YILAI_OFFICIAL);
  check(fresh_official["model_provider"] == "custom" &&
            *selected_provider(fresh_official) == official_provider,
        "Official switching requires no previous configuration or API key.");
  check(!is_yilai(selected_provider(before)) &&
            is_yilai(selected_provider(connected)),
        "Provider identity must inspect endpoint.");
  check(!is_yilai(nullptr), "Built-in OpenAI must not be Yilai.");
  for (const char *bad : {"model = 1\nmodel = 2", "profile = 'missing'",
                          "model_provider = 'missing'"}) {
    bool rejected = false;
    try {
      rewrite(bad, "", YILAI_ENHANCE);
    } catch (...) {
      rejected = true;
    }
    check(rejected, "Invalid configuration accepted.");
  }
  const char *override_input = R"toml(
profile = "work"
model = "gpt-6-astra"
model_catalog_json = "ccs-models.json"
model_provider = "custom"
forced_login_method = "chatgpt"
forced_chatgpt_workspace_id = "workspace"
openai_base_url = "https://old.invalid"
chatgpt_base_url = "https://old.invalid/chat"
approval_policy = "never"
[features]
image_generation = false
multi_agent = true
[model_providers.custom]
base_url = "https://old.invalid"
env_key = "STALE_TOKEN"
[model_providers.custom.http_headers]
Authorization = "synthetic-old-token"
[model_providers.spare]
base_url = "https://spare.invalid"
[profiles.work]
model = "gpt-6-astra"
model_catalog_json = "work-models.json"
model_provider = "custom"
forced_login_method = "api"
forced_chatgpt_workspace_id = "work-space"
openai_base_url = "https://work.invalid"
chatgpt_base_url = "https://work.invalid/chat"
[profiles.work.features]
image_generation = false
[profiles.work.model_providers.custom]
env_key = "WORK_STALE_TOKEN"
[profiles.work.model_providers.spare]
base_url = "https://work-spare.invalid"
[profiles.personal]
model_provider = "custom"
forced_login_method = "chatgpt"
[profiles.personal.features]
image_generation = false
[profiles.personal.model_providers.custom]
base_url = "https://personal.invalid"
[mcp_servers.keep]
command = "keep-command"
[projects."C:/keep-project"]
trust_level = "trusted"
)toml";
  const char *override_expected = R"toml(
profile = "work"
model = "gpt-6-astra"
model_catalog_json = "ccs-models.json"
approval_policy = "never"
[features]
multi_agent = true
[model_providers.spare]
base_url = "https://spare.invalid"
[model_providers.yilai-sync-previous-custom]
base_url = "https://old.invalid"
env_key = "STALE_TOKEN"
[model_providers.yilai-sync-previous-custom.http_headers]
Authorization = "synthetic-old-token"
[profiles.work]
model = "gpt-6-astra"
model_catalog_json = "work-models.json"
[profiles.work.model_providers.spare]
base_url = "https://work-spare.invalid"
[profiles.personal]
model_provider = "yilai-sync-previous-custom"
forced_login_method = "chatgpt"
[profiles.personal.features]
image_generation = false
[profiles.personal.model_providers.custom]
base_url = "https://personal.invalid"
[profiles.personal.model_providers.yilai-sync-previous-custom]
base_url = "https://personal.invalid"
[mcp_servers.keep]
command = "keep-command"
[projects."C:/keep-project"]
trust_level = "trusted"
)toml";
  char *override_error = nullptr;
  auto *override_result =
      yilai_clear_connection_overrides(override_input, &override_error);
  check(override_result && !override_error, "Override rewrite C API failed.");
  auto cleared_override = toml::parse(override_result);
  yilai_config_free(override_result);
  check(cleared_override == toml::parse(override_expected),
        "Override cleanup left connection fields or changed unrelated settings.");
  check(clear_connection_overrides(format(cleared_override).c_str()) ==
            cleared_override,
        "Override cleanup is not idempotent.");
  auto cross_layer_profile = clear_connection_overrides(
      "profile = 'defined-elsewhere'\nmodel_provider = 'custom'\n"
      "[features]\nimage_generation = false\n");
  check(cross_layer_profile == toml::parse("profile = 'defined-elsewhere'\n"),
        "An externally declared profile prevented clearing this layer.");
  check(clear_connection_overrides("").empty(),
        "Empty override cleanup created unrelated settings.");
  const char *unchanged_override =
      "# Preserve comments and original ordering\r\n"
      "model = 'gpt-6-astra'\r\n\r\n"
      "[features] # Empty table is unrelated\r\n"
      "[mcp_servers.keep]\r\ncommand = 'keep'\r\n";
  override_result = yilai_clear_connection_overrides(unchanged_override, &override_error);
  check(override_result && !override_error &&
            std::string(override_result) == unchanged_override,
        "An unchanged override was unnecessarily reformatted.");
  yilai_config_free(override_result);
  auto unshared_override = clear_connection_overrides(
      "model_provider = 'custom'\n[model_providers.custom]\n"
      "base_url = 'https://unused.invalid'\n");
  check(unshared_override["model_providers"].as_table()->empty(),
        "An unreferenced override provider was unnecessarily archived.");
  auto colliding_override = toml::parse(override_input);
  table_at(colliding_override, "model_providers")
      .insert_or_assign("yilai-sync-previous-custom",
                        toml::table{{"name", "Keep root archive"}});
  table_at(table_at(table_at(colliding_override, "profiles"), "personal"),
           "model_providers")
      .insert_or_assign("yilai-sync-previous-custom-2",
                        toml::table{{"name", "Keep inline archive"}});
  auto preserved_collision =
      clear_connection_overrides(format(colliding_override).c_str());
  check(preserved_collision["profiles"]["personal"]["model_provider"] ==
            "yilai-sync-previous-custom-3" &&
            preserved_collision["model_providers"]["yilai-sync-previous-custom"] ==
                colliding_override["model_providers"]["yilai-sync-previous-custom"] &&
            preserved_collision["profiles"]["personal"]["model_providers"]
                               ["yilai-sync-previous-custom-2"] ==
                colliding_override["profiles"]["personal"]["model_providers"]
                                  ["yilai-sync-previous-custom-2"],
        "Archiving a shared override overwrote an existing provider.");
  for (const char *invalid : {static_cast<const char *>(nullptr),
                              "token = 'private-parser-sentinel' invalid",
                              "profile = 1"}) {
    override_result = yilai_clear_connection_overrides(invalid, &override_error);
    check(!override_result && override_error &&
              std::string(override_error).find("private-parser-sentinel") ==
                  std::string::npos,
          "Override cleanup accepted invalid input or leaked parser source.");
    yilai_config_free(override_error);
    override_error = nullptr;
  }
  auto escaped = rewrite("", "key-\"quoted\\value", YILAI_CONFIGURE);
  check(toml::parse(format(escaped)) == escaped,
        "Escaped credentials changed.");
}
} // namespace
extern "C" char *yilai_apply_config(const char *text, const char *key,
                                    int action, char **error) {
  if (error)
    *error = nullptr;
  try {
    if (!text || !key)
      throw std::runtime_error("Missing configuration input.");
    auto *result = copy_string(format(rewrite(text, key, action)));
    if (!result)
      throw std::runtime_error("Out of memory.");
    return result;
  } catch (const toml::parse_error &failure) {
    if (error)
      *error = copy_string("Invalid TOML at line " +
                           std::to_string(failure.source().begin.line) +
                           ", column " +
                           std::to_string(failure.source().begin.column) +
                           ". Original files were not changed.");
  } catch (const std::exception &failure) {
    if (error)
      *error = copy_string(failure.what());
  } catch (...) {
    if (error)
      *error = copy_string("Unable to update configuration.");
  }
  return nullptr;
}
extern "C" char *yilai_clear_connection_overrides(const char *text, char **error) {
  if (error)
    *error = nullptr;
  try {
    if (!text)
      throw std::runtime_error("Missing configuration input.");
    const auto original = toml::parse(text);
    const auto cleared = clear_connection_overrides(text);
    auto *result = copy_string(cleared == original ? std::string(text) : format(cleared));
    if (!result)
      throw std::runtime_error("Out of memory.");
    return result;
  } catch (const toml::parse_error &failure) {
    if (error)
      *error = copy_string("Invalid TOML at line " +
                           std::to_string(failure.source().begin.line) +
                           ", column " +
                           std::to_string(failure.source().begin.column) +
                           ". Original files were not changed.");
  } catch (const std::exception &failure) {
    if (error)
      *error = copy_string(failure.what());
  } catch (...) {
    if (error)
      *error = copy_string("Unable to clear connection overrides.");
  }
  return nullptr;
}
extern "C" void yilai_config_free(char *value) { std::free(value); }
extern "C" int yilai_config_mode(const char *text) {
  try {
    auto root = toml::parse(text);
    auto *provider = selected_provider(root);
    if (!provider || (!(*provider).contains("base_url") &&
                      (*provider)["requires_openai_auth"].value_or(false)))
      return 0;
    return is_yilai(provider) ? 1 : 2;
  } catch (...) {
    return -1;
  }
}
extern "C" int yilai_config_self_test(char **error) {
  if (error)
    *error = nullptr;
  try {
    self_test();
    return 1;
  } catch (const std::exception &failure) {
    if (error)
      *error = copy_string(failure.what());
    return 0;
  } catch (...) {
    if (error)
      *error = copy_string("Configuration self-test failed.");
    return 0;
  }
}
