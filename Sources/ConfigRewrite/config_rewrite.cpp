#include "ConfigRewrite.h"
#include "vendor/toml.hpp"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

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

void overlay(toml::table &destination, const toml::table &source) {
    for (const auto &[key, value] : source) {
        auto *existing = destination[key].as_table();
        auto *incoming = value.as_table();
        if (existing && incoming) overlay(*existing, *incoming);
        else destination.insert_or_assign(key, value);
    }
}

void set_model(toml::table &scope, const char *catalog, bool official) {
    scope.erase("model_provider");
    scope.erase("model_catalog_json");
    scope.insert_or_assign("model", official ? "gpt-5.6-terra" : "gpt-5.6-sol");
    if (!official) {
        scope.insert_or_assign("model_provider", "yilai");
        scope.insert_or_assign("model_catalog_json", catalog);
    }
}

toml::table rewrite(const char *existing, const char *key, const char *catalog, bool official) {
    auto root = toml::parse(existing);
    auto *profile = active_profile(root);
    if (profile) {
        // New runtimes reject the legacy selector. Keep its effective settings.
        if (profile->contains("profile") || profile->contains("profiles")) {
            throw std::runtime_error("Nested profile selectors are not supported.");
        }
        set_model(*profile, catalog, official);
        if (!official && profile->contains("features")) {
            table_at(*profile, "features").insert_or_assign("image_generation", true);
        }
        const toml::table selected = *profile;
        overlay(root, selected);
        root.erase("profile");
    }
    // Authentication restrictions belong to this switch; MCP OAuth is unrelated.
    for (const char *field : {"forced_login_method", "forced_chatgpt_workspace_id", "openai_base_url"}) {
        root.erase(field);
    }
    root.insert_or_assign("cli_auth_credentials_store", "file");
    set_model(root, catalog, official);
    if (official) {
        // Preserve the existing official-reset contract: no custom provider definitions.
        root.erase("model_providers");
    } else {
        table_at(root, "features").insert_or_assign("image_generation", true);
        toml::table provider{
            {"name", "\xE6\x98\x93\xE6\x9D\xA5 API"},
            {"base_url", "https://api.yilai-ai.com"},
            {"wire_api", "responses"},
            {"requires_openai_auth", false},
            {"experimental_bearer_token", key},
            {"http_headers", toml::table{{"x-openai-actor-authorization", "local-image-extension"}}},
        };
        // Replacing the node also removes stale nested headers and auth settings.
        table_at(root, "model_providers").insert_or_assign("yilai", std::move(provider));
    }
    return root;
}

std::string format(const toml::table &root) {
    std::ostringstream output;
    output << toml::toml_formatter{root, toml::format_flags::allow_unicode_strings};
    output << '\n';
    return output.str();
}

char *copy_string(const std::string &text) {
    auto *result = static_cast<char *>(std::malloc(text.size() + 1));
    if (result) std::memcpy(result, text.c_str(), text.size() + 1);
    return result;
}

void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void self_test() {
    const char *old = R"toml(
profile = "work"
model_provider = "old"
model = "old-model"
forced_login_method = "chatgpt"
forced_chatgpt_workspace_id = "old-workspace"
openai_base_url = "https://old.example"
cli_auth_credentials_store = "keyring"
approval_policy = "on-request"
sandbox_mode = "workspace-write"
mcp_oauth_credentials_store = "keyring"
notify = ["keep-notify", "argument"]
developer_instructions = '''Keep this text verbatim:
[model_providers.yilai]
api_key = "this is instruction text, not a setting"
'''
[sandbox_workspace_write]
writable_roots = ["C:/keep-path"]
network_access = true
[features]
image_generation = false
shell_tool = true
[model_providers.yilai] # Old nested credentials must be replaced together.
name = "Old"
base_url = "https://old.example"
requires_openai_auth = true
[model_providers.yilai.http_headers]
Authorization = "Bearer sk-old-test"
[model_providers.yilai.env_http_headers]
Authorization = "OLD_TEST_TOKEN"
[model_providers.other]
name = "Keep this inactive provider"
experimental_bearer_token = "sk-other-test"
[mcp_servers.sample]
command = "keep-command"
args = ["--flag", "a#b"]
[mcp_servers.sample.env]
API_KEY = "keep-mcp-test-key"
[plugins."browser@openai-bundled"]
enabled = true
[projects.'C:\work']
trust_level = "trusted"
[profiles.work]
model = "old-profile-model"
model_provider = "other"
model_catalog_json = "old-profile-catalog.json"
approval_policy = "never"
sandbox_mode = "read-only"
[profiles.work.features]
image_generation = false
shell_tool = false
[profiles.spare]
model_provider = "other"
model = "keep-spare-model"
)toml";
    auto original = toml::parse(old);
    auto result = rewrite(old, "sk-new-test", "/tmp/new-catalog.json", false);
    check(result["model_provider"] == "yilai", "Root provider was not replaced.");
    check(result["features"]["image_generation"] == true, "Image generation is disabled.");
    check(result["profiles"]["work"]["model_provider"] == "yilai", "Active profile overrides provider.");
    check(result["profiles"]["work"]["features"]["image_generation"] == true, "Active profile disables images.");
    check(result["model_providers"]["yilai"]["requires_openai_auth"] == false, "Official auth still required.");
    check(!result["model_providers"]["yilai"]["env_http_headers"], "Old nested credentials remain.");
    check(!result["model_providers"]["yilai"]["http_headers"]["Authorization"], "Old Authorization remains.");
    check(!result.contains("forced_login_method") && !result.contains("openai_base_url"), "Old auth restrictions remain.");
    for (const char *field : {"mcp_servers", "plugins", "projects", "mcp_oauth_credentials_store",
                              "notify", "developer_instructions", "sandbox_workspace_write"}) {
        check(original[field] == result[field], "Unrelated settings changed.");
    }
    check(original["model_providers"]["other"] == result["model_providers"]["other"], "Inactive provider changed.");
    check(original["profiles"]["spare"] == result["profiles"]["spare"], "Inactive profile changed.");
    check(result["profiles"]["work"]["approval_policy"] == "never", "Profile permissions changed.");
    check(result["profiles"]["work"]["sandbox_mode"] == "read-only", "Profile sandbox changed.");
    check(!result.contains("profile"), "Unsupported legacy profile selector remains.");
    check(result["approval_policy"] == "never" && result["sandbox_mode"] == "read-only", "Effective profile permissions changed.");
    check(result["features"]["shell_tool"] == false && result["profiles"]["work"]["features"]["shell_tool"] == false, "Effective profile features changed.");
    auto text = format(result);
    check(toml::parse(text) == result, "Serialized TOML does not round-trip.");
    check(rewrite(text.c_str(), "sk-new-test", "/tmp/new-catalog.json", false) == result, "Rewriting is not idempotent.");
    auto official = rewrite(text.c_str(), "", "", true);
    check(!official.contains("model_providers") && !official.contains("model_provider"), "Official route not restored.");
    check(official["profiles"]["work"]["approval_policy"] == "never", "Official reset changed profile permissions.");
    for (const char *invalid : {"model = \"a\"\nmodel = \"b\"\n", "[features\n", "profile = \"missing\"\n", "features = false\n"}) {
        bool rejected = false;
        try { (void)rewrite(invalid, "test-key", "/tmp/catalog.json", false); }
        catch (...) { rejected = true; }
        check(rejected, "Invalid input was accepted.");
    }
    auto escaped = rewrite("", "sk-\"quoted\\key", "C:\\catalog\"name.json", false);
    check(toml::parse(format(escaped)) == escaped, "Escaped values changed.");
    auto inline_config = rewrite(R"(features = { image_generation = false, shell_tool = true }
model_providers = { yilai = { name = "Old", http_headers = { Authorization = "old-key" } }, other = { name = "Keep" } }
)", "new-key", "/tmp/catalog.json", false);
    check(inline_config["model_providers"]["other"]["name"] == "Keep", "Inline inactive provider changed.");
    check(!inline_config["model_providers"]["yilai"]["http_headers"]["Authorization"], "Inline old credentials remain.");
    check(toml::parse(format(inline_config)) == inline_config, "Inline TOML does not round-trip.");
}
} // namespace

extern "C" char *yilai_rewrite_config(const char *existing, const char *key,
                                       const char *catalog_path, int official, char **error) {
    if (error) *error = nullptr;
    try {
        if (!existing || !key || !catalog_path) throw std::runtime_error("Missing configuration input.");
        auto result = copy_string(format(rewrite(existing, key, catalog_path, official != 0)));
        if (!result) throw std::runtime_error("Out of memory.");
        return result;
    } catch (const toml::parse_error &failure) {
        // Never include the source text: configuration may contain credentials.
        if (error) *error = copy_string("Invalid TOML at line " + std::to_string(failure.source().begin.line)
            + ", column " + std::to_string(failure.source().begin.column) + ". Original files were not changed.");
    } catch (const std::exception &failure) {
        if (error) *error = copy_string(failure.what());
    } catch (...) {
        if (error) *error = copy_string("Unable to rewrite configuration.");
    }
    return nullptr;
}

extern "C" void yilai_config_free(char *value) { std::free(value); }

extern "C" int yilai_config_mode(const char *text) {
    try {
        auto root = toml::parse(text);
        auto *profile = active_profile(root);
        auto provider = profile && profile->contains("model_provider")
            ? (*profile)["model_provider"].value<std::string>() : root["model_provider"].value<std::string>();
        if (!provider || *provider == "openai") return 0;
        return *provider == "yilai" ? 1 : 2;
    } catch (...) { return -1; }
}

extern "C" int yilai_verify_config(const char *text, const char *key, const char *catalog_path) {
    try { return toml::parse(text) == rewrite(text, key, catalog_path, false) ? 1 : 0; }
    catch (...) { return 0; }
}

extern "C" int yilai_config_self_test(char **error) {
    if (error) *error = nullptr;
    try { self_test(); return 1; }
    catch (const std::exception &failure) {
        if (error) *error = copy_string(failure.what());
        return 0;
    }
    catch (...) {
        if (error) *error = copy_string("Structured configuration self-test failed.");
        return 0;
    }
}
