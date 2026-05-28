#include "app/routes.h"
#include "plugins/database_migration_plugin.h"

#include <drogon/drogon.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::optional<std::string> config_path_from(std::span<char* const> args)
{
    for(std::size_t i = 1; i + 1 < args.size(); ++i) {
        if(std::string_view{args[i]} == "--config") {
            return std::string{args[i + 1]};
        }
    }

    return std::nullopt;
}

}

int main(int argc, char* argv[])
{
    blogalone::plugins::ensure_database_migration_plugin_registered();
    blogalone::register_routes();

    const auto args = std::span<char* const>{argv, static_cast<std::size_t>(argc)};
    if(const auto config_path = config_path_from(args)) {
        drogon::app().loadConfigFile(*config_path);
    } else {
        drogon::app().addListener("127.0.0.1", 8080);
        drogon::app().setThreadNum(1);
    }

    drogon::app().run();
}
