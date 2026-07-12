#include "cli/command_line.h"

#include <cstddef>
#include <optional>

namespace blogalone::cli {
namespace {

constexpr std::string_view kDefaultProductionConfig = "/etc/blogalone/config.json";

[[nodiscard]] std::string_view required_value(
    std::span<const std::string_view> args,
    std::size_t& index,
    std::string_view option
)
{
    if(index + 1 >= args.size() || args[index + 1].starts_with("--")) {
        throw CommandLineError{"missing value for " + std::string{option}};
    }
    ++index;
    return args[index];
}

[[nodiscard]] CommandMode command_mode(std::span<const std::string_view> args, std::size_t& index)
{
    if(args.empty()) {
        return CommandMode::serve;
    }
    if(args.front() == "--help" || args.front() == "-h") {
        ++index;
        return CommandMode::help;
    }
    if(args.front() == "--check-config") {
        ++index;
        return CommandMode::check_config;
    }
    if(args.front() != "admin") {
        return CommandMode::serve;
    }
    if(args.size() < 2 || args[1] != "create") {
        throw CommandLineError{"expected 'admin create'"};
    }
    index = 2;
    return CommandMode::create_admin;
}

}

CommandLineOptions parse_command_line(std::span<const std::string_view> args)
{
    std::size_t index = 0;
    CommandLineOptions options{.mode = command_mode(args, index)};
    if(options.mode == CommandMode::create_admin) {
        options.config_path = kDefaultProductionConfig;
    }

    while(index < args.size()) {
        const auto option = args[index];
        if(option == "--config") {
            options.config_path = required_value(args, index, option);
        } else if(option == "--username" && options.mode == CommandMode::create_admin) {
            options.username = required_value(args, index, option);
        } else if(option == "--password-file" && options.mode == CommandMode::create_admin) {
            options.password_file = required_value(args, index, option);
        } else if(option == "--force" && options.mode == CommandMode::create_admin) {
            options.force = true;
        } else {
            throw CommandLineError{"unknown option: " + std::string{option}};
        }
        ++index;
    }

    if(options.mode == CommandMode::help) {
        return options;
    }
    if(options.config_path.empty()) {
        throw CommandLineError{"--config <path> is required"};
    }
    if(options.mode == CommandMode::create_admin) {
        if(options.username.empty()) {
            throw CommandLineError{"--username <name> is required"};
        }
        if(options.password_file.empty()) {
            throw CommandLineError{"--password-file <path> is required"};
        }
    }
    return options;
}

std::string_view command_line_usage()
{
    return
        "Usage:\n"
        "  blogalone --config <path>\n"
        "  blogalone --check-config --config <path>\n"
        "  blogalone admin create --username <name> --password-file <path> "
        "[--config <path>] [--force]\n";
}

}
