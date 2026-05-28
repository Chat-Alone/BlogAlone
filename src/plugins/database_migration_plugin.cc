#include "plugins/database_migration_plugin.h"

#include "db/migrations.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace blogalone::plugins {
namespace {

[[nodiscard]] std::string required_string(const Json::Value& config, const char* key)
{
    if(!config.isMember(key) || !config[key].isString() || config[key].asString().empty()) {
        throw std::invalid_argument{std::string{"database migration plugin requires config."} + key};
    }
    return config[key].asString();
}

void configure_drogon_sqlite_client(const std::string& db_client_name)
{
    const auto client = drogon::app().getDbClient(db_client_name);
    client->execSqlSync("PRAGMA foreign_keys = ON;");
    client->execSqlSync("PRAGMA journal_mode = WAL;");
    client->execSqlSync("PRAGMA synchronous = NORMAL;");
    client->execSqlSync("PRAGMA busy_timeout = 5000;");
}

}

void DatabaseMigrationPlugin::initAndStart(const Json::Value& config)
{
    const auto database_path = std::filesystem::path{required_string(config, "database_path")};
    const auto migrations_dir = std::filesystem::path{
        config.get("migrations_dir", "migrations").asString()
    };
    const auto db_client_name = config.get("db_client", "default").asString();

    const auto applied = db::run_migrations(db::MigrationOptions{
        .database_path = database_path,
        .migrations_dir = migrations_dir
    });
    configure_drogon_sqlite_client(db_client_name);

    spdlog::info("Database migrations complete; applied {} migration(s)", applied.size());
}

void DatabaseMigrationPlugin::shutdown()
{
}

void ensure_database_migration_plugin_registered()
{
    static_cast<void>(DatabaseMigrationPlugin::classTypeName());
}

}
