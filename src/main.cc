#include "app/routes.h"
#include "cli/admin_command.h"
#include "cli/command_line.h"
#include "config/config_file.h"
#include "plugins/database_migration_plugin.h"
#include "plugins/session_cleanup_plugin.h"
#include "plugins/upload_cleanup_plugin.h"

#include <drogon/drogon.h>

#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::string_view> argument_views(std::span<char* const> args)
{
    std::vector<std::string_view> views;
    if(args.size() > 1) {
        views.reserve(args.size() - 1);
    }
    for(std::size_t index = 1; index < args.size(); ++index) {
        views.emplace_back(args[index]);
    }
    return views;
}

}

int main(int argc, char* argv[])
{
    try {
        const auto raw_args = std::span<char* const>{argv, static_cast<std::size_t>(argc)};
        const auto args = argument_views(raw_args);
        const auto options = blogalone::cli::parse_command_line(args);

        if(options.mode == blogalone::cli::CommandMode::help) {
            std::cout << blogalone::cli::command_line_usage();
            return 0;
        }

        const auto config = blogalone::config::load_config_file(options.config_path);
        if(options.mode == blogalone::cli::CommandMode::check_config) {
            blogalone::config::check_runtime_paths(config);
            std::cout << "Configuration is valid: " << options.config_path.string() << '\n';
            return 0;
        }
        if(options.mode == blogalone::cli::CommandMode::create_admin) {
            const auto user_id = blogalone::cli::create_admin(blogalone::cli::AdminCreateOptions{
                .database_path = config.database_path,
                .password_file = options.password_file,
                .username = options.username,
                .password_hash_options = config.app.password_hash_options,
                .force = options.force
            });
            std::cout << "Administrator created with user id " << user_id << '\n';
            return 0;
        }

        blogalone::config::check_runtime_paths(config);
        blogalone::plugins::ensure_database_migration_plugin_registered();
        blogalone::plugins::ensure_session_cleanup_plugin_registered();
        blogalone::plugins::ensure_upload_cleanup_plugin_registered();
        drogon::app().loadConfigFile(options.config_path.string());
        blogalone::register_routes();
        drogon::app().run();
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "blogalone: " << error.what() << '\n';
        std::cerr << blogalone::cli::command_line_usage();
        return 1;
    }
}
