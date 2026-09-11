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
    if (!parent.contains(key)) parent.insert(key, toml::table{});
    auto *table = parent[key].as_table();
    if (!table) throw std::runtime_error(std::string("Expected a TOML table: ") + key);
    return *table;
}
toml::table *active_profile(toml::table &root) {
    if (!root.contains("profile")) return nullptr;
    auto name = root["profile"].value<std::string>();
    if (!name) throw std::runtime_error("The selected profile must be a string.");
    auto *profiles = root["profiles"].as_table();
    auto *profile = profiles ? (*profiles)[*name].as_table() : nullptr;
    if (!profile) throw std::runtime_error("The selected profile does not exist.");
    return profile;
}
std::string provider_id(toml::table &root) {
    auto *profile = active_profile(root);
    auto &scope = profile && profile->contains("model_provider") ? *profile : root;
    if (!scope.contains("model_provider")) return "openai";
    auto id = scope["model_provider"].value<std::string>();
    if (!id || id->empty()) throw std::runtime_error("Invalid model_provider.");
    return *id;
}
toml::table *selected_provider(toml::table &root) {
    auto id = provider_id(root);
    auto *providers = root["model_providers"].as_table();
    auto *provider = providers ? (*providers)[id].as_table() : nullptr;
    if (!provider && id != "openai") throw std::runtime_error("Selected provider is missing. Reapply the connection in CCS first.");
    return provider;
}
bool is_yilai(const toml::table *provider) {
    if (!provider) return false;
    auto url = (*provider)["base_url"].value_or(std::string());
    std::transform(url.begin(), url.end(), url.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string host = "https://api.yilai-ai.com";
    return url == host || url.rfind(host + "/", 0) == 0;
}
void enhance(toml::table &root) {
    auto *provider = selected_provider(root);
    // These are the only changes made by the default operation.
    table_at(root, "features").insert_or_assign("image_generation", true);
    if (auto *profile = active_profile(root)) table_at(*profile, "features").insert_or_assign("image_generation", true);
    if (provider) table_at(*provider, "http_headers").insert_or_assign("x-openai-actor-authorization", "local-image-extension");
}
void remove_owned_catalog(toml::table &scope) {
    auto value = scope["model_catalog_json"].value_or(std::string());
    auto offset = value.find_last_of("/\\");
    if (value.substr(offset == std::string::npos ? 0 : offset + 1) == "yilai-model-catalog.json") scope.erase("model_catalog_json");
}
toml::table rewrite(const char *text, const char *key, int action) {
    auto root = toml::parse(text);
    auto *profile = active_profile(root);
    if (action == YILAI_ENHANCE) {
        if (root.empty()) throw std::runtime_error("Configure a connection in CCS first, or use Configure Yilai with an API key.");
        enhance(root);
    } else if (action == YILAI_CONFIGURE) {
        const std::string token(key);
        if (token.empty() || std::any_of(token.begin(), token.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::runtime_error("A non-empty API key without control characters is required.");
        // Explicit connection configuration, not the default image operation.
        // CCS uses custom for its third-party session bucket. Model/catalog/auth files stay intact.
        root.insert_or_assign("model_provider", "custom");
        if (profile) profile->insert_or_assign("model_provider", "custom");
        toml::table provider{{"name", "易来 API"}, {"base_url", "https://api.yilai-ai.com"},
            {"wire_api", "responses"}, {"requires_openai_auth", false}, {"experimental_bearer_token", token}};
        table_at(root, "model_providers").insert_or_assign("custom", std::move(provider));
        enhance(root);
    } else if (action == YILAI_CLEANUP) {
        auto *provider = selected_provider(root);
        if (!is_yilai(provider) || (*provider)["experimental_bearer_token"].value_or(std::string()).empty()
            || (*provider)["requires_openai_auth"].value_or(true))
            throw std::runtime_error("Cleanup requires a direct Yilai connection with its own bearer key. Use Configure Yilai first; official/CCS credentials are protected.");
        for (auto *scope : {&root, profile}) if (scope) {
            for (const char *field : {"forced_login_method", "forced_chatgpt_workspace_id", "openai_base_url"}) scope->erase(field);
            scope->insert_or_assign("cli_auth_credentials_store", "file");
            remove_owned_catalog(*scope);
        }
    } else if (action == YILAI_UNIFY_HISTORY) {
        auto id = provider_id(root);
        auto *current = selected_provider(root);
        if (id != "custom") {
            toml::table unified = current ? *current : toml::table{{"name", "OpenAI"},
                {"wire_api", "responses"}, {"requires_openai_auth", true}, {"supports_websockets", true}};
            auto &providers = table_at(root, "model_providers");
            if (auto *previous = providers["custom"].as_table()) {
                std::string spare = "yilai-sync-previous-custom";
                for (int i = 2; providers.contains(spare); ++i) spare = "yilai-sync-previous-custom-" + std::to_string(i);
                providers.insert_or_assign(spare, *previous);
            }
            providers.insert_or_assign("custom", std::move(unified));
            root.insert_or_assign("model_provider", "custom");
            if (profile) profile->insert_or_assign("model_provider", "custom");
        }
    } else throw std::runtime_error("Unknown configuration operation.");
    return root;
}
std::string format(const toml::table &root) {
    std::ostringstream out;
    out << toml::toml_formatter{root, toml::format_flags::allow_unicode_strings} << '\n';
    return out.str();
}
char *copy_string(const std::string &text) {
    auto *result = static_cast<char *>(std::malloc(text.size() + 1));
    if (result) std::memcpy(result, text.c_str(), text.size() + 1);
    return result;
}
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
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
    table_at(*active_profile(expected), "features").insert_or_assign("image_generation", true);
    table_at(*selected_provider(expected), "http_headers").insert_or_assign("x-openai-actor-authorization", "local-image-extension");
    auto enabled = rewrite(ccs, "", YILAI_ENHANCE);
    check(enabled == expected, "Image enhancement modified unrelated CCS values.");
    check(rewrite(format(enabled).c_str(), "", YILAI_ENHANCE) == enabled, "Enhancement is not idempotent.");
    auto connected = rewrite(ccs, "new-key", YILAI_CONFIGURE);
    check(connected["model"] == before["model"] && connected["model_catalog_json"] == before["model_catalog_json"], "Configuration changed model/catalog.");
    check(connected["forced_login_method"] == before["forced_login_method"] && connected["cli_auth_credentials_store"] == before["cli_auth_credentials_store"], "Configuration performed cleanup.");
    check(connected["model_providers"]["spare"] == before["model_providers"]["spare"], "Inactive provider changed.");
    auto cleared = rewrite(format(connected).c_str(), "", YILAI_CLEANUP);
    check(!cleared.contains("forced_login_method") && cleared["model_catalog_json"] == before["model_catalog_json"], "Cleanup did not preserve CCS catalog.");
    auto owned = connected;
    owned.insert_or_assign("model_catalog_json", "C:\\codex\\yilai-model-catalog.json");
    check(!rewrite(format(owned).c_str(), "", YILAI_CLEANUP).contains("model_catalog_json"), "Owned stale catalog pointer not cleared.");
    for (auto action : {YILAI_CLEANUP}) {
        bool rejected = false;
        try { rewrite(ccs, "", action); } catch (...) { rejected = true; }
        check(rejected, "Other provider credentials must not be cleaned.");
    }
    auto repaired = rewrite(ccs, "", YILAI_UNIFY_HISTORY);
    check(repaired == before, "Legacy alias does not use current route.");
    auto official = rewrite("model = 'gpt-6-astra'\n", "", YILAI_UNIFY_HISTORY);
    check(official["model_provider"] == "custom" && official["model_providers"]["custom"]["requires_openai_auth"] == true
        && !official["model_providers"]["custom"]["experimental_bearer_token"], "Official alias carries third-party credentials.");
    check(!is_yilai(selected_provider(before)) && is_yilai(selected_provider(connected)), "Provider identity must inspect endpoint.");
    check(!is_yilai(nullptr), "Built-in OpenAI must not be Yilai.");
    for (const char *bad : {"model = 1\nmodel = 2", "profile = 'missing'", "model_provider = 'missing'"}) {
        bool rejected = false;
        try { rewrite(bad, "", YILAI_ENHANCE); } catch (...) { rejected = true; }
        check(rejected, "Invalid configuration accepted.");
    }
    auto escaped = rewrite("", "key-\"quoted\\value", YILAI_CONFIGURE);
    check(toml::parse(format(escaped)) == escaped, "Escaped credentials changed.");
}
}
extern "C" char *yilai_apply_config(const char *text, const char *key, int action, char **error) {
    if (error) *error = nullptr;
    try {
        if (!text || !key) throw std::runtime_error("Missing configuration input.");
        auto *result = copy_string(format(rewrite(text, key, action)));
        if (!result) throw std::runtime_error("Out of memory.");
        return result;
    } catch (const toml::parse_error &failure) {
        if (error) *error = copy_string("Invalid TOML at line " + std::to_string(failure.source().begin.line)
            + ", column " + std::to_string(failure.source().begin.column) + ". Original files were not changed.");
    } catch (const std::exception &failure) { if (error) *error = copy_string(failure.what()); }
    catch (...) { if (error) *error = copy_string("Unable to update configuration."); }
    return nullptr;
}
extern "C" void yilai_config_free(char *value) { std::free(value); }
extern "C" int yilai_config_mode(const char *text) {
    try {
        auto root = toml::parse(text);
        auto *provider = selected_provider(root);
        if (!provider || (!(*provider).contains("base_url") && (*provider)["requires_openai_auth"].value_or(false))) return 0;
        return is_yilai(provider) ? 1 : 2;
    } catch (...) { return -1; }
}
extern "C" int yilai_config_self_test(char **error) {
    if (error) *error = nullptr;
    try { self_test(); return 1; }
    catch (const std::exception &failure) { if (error) *error = copy_string(failure.what()); return 0; }
    catch (...) { if (error) *error = copy_string("Configuration self-test failed."); return 0; }
}
