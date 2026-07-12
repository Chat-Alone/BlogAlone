#pragma once

#include "config/app_config.h"

#include <filesystem>
#include <stdexcept>

namespace blogalone::config {

struct ConfigFile {
    AppConfig app;
    std::filesystem::path database_path;
    std::filesystem::path migrations_dir;
};

class ConfigFileError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] ConfigFile load_config_file(const std::filesystem::path& path);
void check_runtime_paths(const ConfigFile& config);

}
