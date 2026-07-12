#include "config/config_file.h"

#include <json/reader.h>
#include <json/value.h>

#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace blogalone::config {
namespace {

[[nodiscard]] Json::Value read_json(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    if(!input) {
        throw ConfigFileError{"unable to read config file: " + path.string()};
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if(!Json::parseFromStream(builder, input, &root, &errors)) {
        throw ConfigFileError{"invalid JSON config: " + errors};
    }
    if(!root.isObject()) {
        throw ConfigFileError{"config root must be an object"};
    }
    return root;
}

[[nodiscard]] const Json::Value& required_array(
    const Json::Value& root,
    std::string_view key
)
{
    const std::string key_name{key};
    const auto& value = root[key_name];
    if(!value.isArray() || value.empty()) {
        throw ConfigFileError{key_name + " must be a non-empty array"};
    }
    return value;
}

[[nodiscard]] std::filesystem::path sqlite_database_path(const Json::Value& root)
{
    std::optional<std::filesystem::path> database_path;
    for(const auto& client : required_array(root, "db_clients")) {
        if(!client.isObject() || client["name"].asString() != "default") {
            continue;
        }
        if(client["rdbms"].asString() != "sqlite3") {
            throw ConfigFileError{"default database client must use sqlite3"};
        }
        if(!client["filename"].isString() || client["filename"].asString().empty()) {
            throw ConfigFileError{"default SQLite filename must be a non-empty string"};
        }
        if(!client["number_of_connections"].isInt()
            || client["number_of_connections"].asInt() != 1) {
            throw ConfigFileError{"default SQLite client must use one connection"};
        }
        database_path = std::filesystem::path{client["filename"].asString()};
        break;
    }
    if(!database_path.has_value()) {
        throw ConfigFileError{"default SQLite database client is required"};
    }
    return *database_path;
}

[[nodiscard]] std::filesystem::path migration_directory(
    const Json::Value& root,
    const std::filesystem::path& database_path
)
{
    for(const auto& plugin : required_array(root, "plugins")) {
        if(!plugin.isObject()
            || plugin["name"].asString()
                != "blogalone::plugins::DatabaseMigrationPlugin") {
            continue;
        }

        const auto& plugin_config = plugin["config"];
        if(!plugin_config.isObject()) {
            throw ConfigFileError{"database migration plugin config must be an object"};
        }
        if(plugin_config["db_client"].asString() != "default") {
            throw ConfigFileError{"database migration plugin must use the default client"};
        }
        if(plugin_config["database_path"].asString() != database_path.string()) {
            throw ConfigFileError{"database migration path must match the default SQLite filename"};
        }
        if(!plugin_config["migrations_dir"].isString()
            || plugin_config["migrations_dir"].asString().empty()) {
            throw ConfigFileError{"migration directory must be a non-empty string"};
        }
        return std::filesystem::path{plugin_config["migrations_dir"].asString()};
    }
    throw ConfigFileError{"database migration plugin is required"};
}

void require_directory(const std::filesystem::path& path, std::string_view label)
{
    std::error_code error;
    const auto is_directory = std::filesystem::is_directory(path, error);
    if(error || !is_directory) {
        throw ConfigFileError{std::string{label} + " does not exist: " + path.string()};
    }
}

void require_parent_directory(const std::filesystem::path& path, std::string_view label)
{
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
    require_directory(parent, label);
}

}

ConfigFile load_config_file(const std::filesystem::path& path)
{
    const auto root = read_json(path);
    const auto& custom_config = root["custom_config"];
    if(!custom_config.isObject()) {
        throw ConfigFileError{"custom_config must be an object"};
    }

    try {
        auto app = app_config_from_json(custom_config);
        auto database_path = sqlite_database_path(root);
        auto migrations_dir = migration_directory(root, database_path);
        return ConfigFile{
            .app = std::move(app),
            .database_path = std::move(database_path),
            .migrations_dir = std::move(migrations_dir)
        };
    } catch(const ConfigFileError&) {
        throw;
    } catch(const std::exception& error) {
        throw ConfigFileError{error.what()};
    }
}

void check_runtime_paths(const ConfigFile& config)
{
    require_directory(config.app.web_root, "web root");
    require_directory(config.app.uploads_root, "uploads root");
    require_directory(config.migrations_dir, "migration directory");
    require_parent_directory(config.database_path, "database parent directory");
}

}
