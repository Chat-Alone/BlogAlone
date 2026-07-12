#pragma once

#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace blogalone::cli {

enum class CommandMode {
    serve,
    check_config,
    create_admin,
    help
};

struct CommandLineOptions {
    CommandMode mode{CommandMode::serve};
    std::filesystem::path config_path;
    std::filesystem::path password_file;
    std::string username;
    bool force{};
};

class CommandLineError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] CommandLineOptions parse_command_line(std::span<const std::string_view> args);
[[nodiscard]] std::string_view command_line_usage();

}
