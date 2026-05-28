#pragma once

#include <drogon/plugins/Plugin.h>

namespace blogalone::plugins {

class DatabaseMigrationPlugin final : public drogon::Plugin<DatabaseMigrationPlugin> {
  public:
    DatabaseMigrationPlugin() = default;

    void initAndStart(const Json::Value& config) override;
    void shutdown() override;
};

void ensure_database_migration_plugin_registered();

}
