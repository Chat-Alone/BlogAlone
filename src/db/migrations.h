#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace blogalone::db {

struct MigrationOptions {
    std::filesystem::path database_path;
    std::filesystem::path migrations_dir;
};

struct AppliedMigration {
    int version{};
    std::filesystem::path path;
    std::string checksum;
};

class MigrationError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::vector<AppliedMigration> run_migrations(const MigrationOptions& options);

}
