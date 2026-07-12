#pragma once

#include "util/password.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace blogalone::cli {

struct AdminCreateOptions {
    std::filesystem::path database_path;
    std::filesystem::path password_file;
    std::string username;
    util::PasswordHashOptions password_hash_options;
    bool force{};
};

class AdminCommandError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::int64_t create_admin(const AdminCreateOptions& options);

}
