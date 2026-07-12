#include "cli/admin_command.h"
#include "cli/command_line.h"
#include "config/config_file.h"
#include "db/migrations.h"
#include "util/password.h"

#include <drogon/drogon.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

class TempWorkspace {
  public:
    TempWorkspace()
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        for(int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = base
                / ("blogalone-cli-test-" + std::to_string(ticks) + "-"
                    + std::to_string(attempt));
            if(std::filesystem::create_directory(candidate)) {
                path_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error{"unable to create temporary test directory"};
    }

    TempWorkspace(const TempWorkspace&) = delete;
    TempWorkspace& operator=(const TempWorkspace&) = delete;

    ~TempWorkspace()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream output{path, std::ios::binary};
    if(!output) {
        throw std::runtime_error{"unable to write test file"};
    }
    output << content;
}

[[nodiscard]] blogalone::util::PasswordHashOptions fast_password_options()
{
    return blogalone::util::PasswordHashOptions{
        .opslimit = 1,
        .memlimit = 8'192
    };
}

}

TEST(CommandLineTest, ParsesConfigCheckAndAdminCreate)
{
    const std::array check_args{
        std::string_view{"--check-config"},
        std::string_view{"--config"},
        std::string_view{"config/test.json"}
    };
    const auto check = blogalone::cli::parse_command_line(check_args);
    EXPECT_EQ(check.mode, blogalone::cli::CommandMode::check_config);
    EXPECT_EQ(check.config_path, std::filesystem::path{"config/test.json"});

    const std::array admin_args{
        std::string_view{"admin"},
        std::string_view{"create"},
        std::string_view{"--username"},
        std::string_view{"root_admin"},
        std::string_view{"--password-file"},
        std::string_view{"secret.txt"},
        std::string_view{"--force"}
    };
    const auto admin = blogalone::cli::parse_command_line(admin_args);
    EXPECT_EQ(admin.mode, blogalone::cli::CommandMode::create_admin);
    EXPECT_EQ(admin.config_path, std::filesystem::path{"/etc/blogalone/config.json"});
    EXPECT_EQ(admin.username, "root_admin");
    EXPECT_EQ(admin.password_file, std::filesystem::path{"secret.txt"});
    EXPECT_TRUE(admin.force);
}

TEST(CommandLineTest, RejectsMissingRequiredArguments)
{
    const std::array server_args{std::string_view{"--config"}};
    EXPECT_THROW(
        static_cast<void>(blogalone::cli::parse_command_line(server_args)),
        blogalone::cli::CommandLineError
    );

    const std::array admin_args{
        std::string_view{"admin"},
        std::string_view{"create"},
        std::string_view{"--username"},
        std::string_view{"root_admin"}
    };
    EXPECT_THROW(
        static_cast<void>(blogalone::cli::parse_command_line(admin_args)),
        blogalone::cli::CommandLineError
    );
}

TEST(CommandLineTest, LoadsDevelopmentAndProductionConfigs)
{
    const auto source_dir = std::filesystem::path{BLOGALONE_SOURCE_DIR};
    const auto development = blogalone::config::load_config_file(
        source_dir / "config" / "config.development.json"
    );
    const auto production = blogalone::config::load_config_file(
        source_dir / "config" / "config.production.json"
    );

    EXPECT_EQ(development.database_path, std::filesystem::path{"blogalone.dev.db"});
    EXPECT_EQ(development.migrations_dir, std::filesystem::path{"migrations"});
    EXPECT_EQ(development.app.web_root, std::filesystem::path{"web"});
    EXPECT_EQ(
        production.database_path,
        std::filesystem::path{"/var/lib/blogalone/blogalone.db"}
    );
    EXPECT_EQ(
        production.migrations_dir,
        std::filesystem::path{"/opt/blogalone/migrations"}
    );
    EXPECT_EQ(production.app.web_root, std::filesystem::path{"/opt/blogalone/web"});
}

TEST(CommandLineTest, CreatesInitialAdminAndAuditsForcedCreation)
{
    TempWorkspace workspace;
    const auto database_path = workspace.path() / "blogalone.db";
    const auto password_file = workspace.path() / "password.txt";
    write_file(password_file, "correct horse battery staple\n");

    static_cast<void>(blogalone::db::run_migrations(blogalone::db::MigrationOptions{
        .database_path = database_path,
        .migrations_dir = std::filesystem::path{BLOGALONE_SOURCE_DIR} / "migrations"
    }));

    const auto initial_id = blogalone::cli::create_admin(blogalone::cli::AdminCreateOptions{
        .database_path = database_path,
        .password_file = password_file,
        .username = "initial_admin",
        .password_hash_options = fast_password_options()
    });
    EXPECT_GT(initial_id, 0);

    EXPECT_THROW(
        static_cast<void>(blogalone::cli::create_admin(blogalone::cli::AdminCreateOptions{
            .database_path = database_path,
            .password_file = password_file,
            .username = "second_admin",
            .password_hash_options = fast_password_options()
        })),
        blogalone::cli::AdminCommandError
    );

    const auto forced_id = blogalone::cli::create_admin(blogalone::cli::AdminCreateOptions{
        .database_path = database_path,
        .password_file = password_file,
        .username = "second_admin",
        .password_hash_options = fast_password_options(),
        .force = true
    });

    const auto client = drogon::orm::DbClient::newSqlite3Client(
        "filename=" + database_path.generic_string(),
        1
    );
    const auto users = client->execSqlSync(
        "SELECT username, pwd_hash, role FROM users ORDER BY id"
    );
    ASSERT_EQ(users.size(), 2);
    EXPECT_EQ(users.at(0)["role"].as<std::string>(), "admin");
    EXPECT_TRUE(blogalone::util::verify_password(
        "correct horse battery staple",
        users.at(0)["pwd_hash"].as<std::string>()
    ));

    const auto logs = client->execSqlSync(
        "SELECT admin_id, action, target_id FROM audit_log ORDER BY id"
    );
    ASSERT_EQ(logs.size(), 1);
    EXPECT_TRUE(logs.at(0)["admin_id"].isNull());
    EXPECT_EQ(logs.at(0)["action"].as<std::string>(), "admin.bootstrap_force");
    EXPECT_EQ(logs.at(0)["target_id"].as<std::int64_t>(), forced_id);
}
