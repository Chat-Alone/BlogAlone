#include "db/migrations.h"

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
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
                / ("blogalone-migration-test-" + std::to_string(ticks) + "-" + std::to_string(attempt));
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

struct SqliteDeleter {
    void operator()(sqlite3* database) const noexcept
    {
        if(database != nullptr) {
            sqlite3_close(database);
        }
    }
};

struct StatementDeleter {
    void operator()(sqlite3_stmt* statement) const noexcept
    {
        if(statement != nullptr) {
            sqlite3_finalize(statement);
        }
    }
};

using SqlitePtr = std::unique_ptr<sqlite3, SqliteDeleter>;
using StatementPtr = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

class TestDatabase {
  public:
    explicit TestDatabase(const std::filesystem::path& path)
    {
        sqlite3* database = nullptr;
        const auto result = sqlite3_open_v2(
            path.string().c_str(),
            &database,
            SQLITE_OPEN_READWRITE,
            nullptr
        );
        database_.reset(database);
        if(result != SQLITE_OK) {
            throw std::runtime_error{"unable to open test database"};
        }
    }

    [[nodiscard]] bool table_exists(std::string_view table_name) const
    {
        auto statement = prepare("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ? LIMIT 1");
        sqlite3_bind_text(
            statement.get(),
            1,
            table_name.data(),
            static_cast<int>(table_name.size()),
            SQLITE_TRANSIENT
        );
        return sqlite3_step(statement.get()) == SQLITE_ROW;
    }

    [[nodiscard]] int scalar_int(std::string_view sql) const
    {
        auto statement = prepare(sql);
        if(sqlite3_step(statement.get()) != SQLITE_ROW) {
            throw std::runtime_error{"query returned no rows"};
        }
        return sqlite3_column_int(statement.get(), 0);
    }

    [[nodiscard]] std::string scalar_text(std::string_view sql) const
    {
        auto statement = prepare(sql);
        if(sqlite3_step(statement.get()) != SQLITE_ROW) {
            throw std::runtime_error{"query returned no rows"};
        }

        const auto* text = sqlite3_column_text(statement.get(), 0);
        if(text == nullptr) {
            return {};
        }
        return reinterpret_cast<const char*>(text);
    }

  private:
    [[nodiscard]] StatementPtr prepare(std::string_view sql) const
    {
        sqlite3_stmt* statement = nullptr;
        const auto sql_text = std::string{sql};
        const auto result = sqlite3_prepare_v2(
            database_.get(),
            sql_text.c_str(),
            -1,
            &statement,
            nullptr
        );
        StatementPtr prepared{statement};
        if(result != SQLITE_OK) {
            throw std::runtime_error{"unable to prepare test query"};
        }
        return prepared;
    }

    SqlitePtr database_;
};

void write_file(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream file{path, std::ios::binary};
    if(!file) {
        throw std::runtime_error{"unable to write test file"};
    }
    file << content;
}

[[nodiscard]] std::filesystem::path make_migrations_dir(const TempWorkspace& workspace)
{
    const auto migrations_dir = workspace.path() / "migrations";
    std::filesystem::create_directory(migrations_dir);
    return migrations_dir;
}

constexpr std::string_view kSchemaMigrationsSql = R"sql(
CREATE TABLE schema_migrations (
    version INTEGER PRIMARY KEY,
    checksum TEXT NOT NULL,
    applied_at INTEGER NOT NULL
);
)sql";

}

TEST(MigrationRunnerTest, AppliesMigrationOnceAndSkipsRepeatedRuns)
{
    TempWorkspace workspace;
    const auto migrations_dir = make_migrations_dir(workspace);
    const auto database_path = workspace.path() / "blogalone.db";
    write_file(
        migrations_dir / "001_init.sql",
        std::string{kSchemaMigrationsSql} + "CREATE TABLE users(id INTEGER PRIMARY KEY);\n"
    );

    const auto first = blogalone::db::run_migrations({
        .database_path = database_path,
        .migrations_dir = migrations_dir
    });
    const auto second = blogalone::db::run_migrations({
        .database_path = database_path,
        .migrations_dir = migrations_dir
    });

    TestDatabase database{database_path};
    EXPECT_EQ(first.size(), 1);
    EXPECT_TRUE(second.empty());
    EXPECT_TRUE(database.table_exists("users"));
    EXPECT_EQ(database.scalar_int("SELECT COUNT(*) FROM schema_migrations"), 1);
}

TEST(MigrationRunnerTest, RollsBackFailedMigration)
{
    TempWorkspace workspace;
    const auto migrations_dir = make_migrations_dir(workspace);
    const auto database_path = workspace.path() / "blogalone.db";
    write_file(
        migrations_dir / "001_init.sql",
        std::string{kSchemaMigrationsSql}
            + "CREATE TABLE before_failure(id INTEGER PRIMARY KEY);\n"
            + "INSERT INTO missing_table(id) VALUES(1);\n"
    );

    EXPECT_THROW(
        static_cast<void>(blogalone::db::run_migrations({
            .database_path = database_path,
            .migrations_dir = migrations_dir
        })),
        blogalone::db::MigrationError
    );

    TestDatabase database{database_path};
    EXPECT_FALSE(database.table_exists("before_failure"));
    EXPECT_FALSE(database.table_exists("schema_migrations"));
}

TEST(MigrationRunnerTest, RejectsChangedChecksumForAppliedVersion)
{
    TempWorkspace workspace;
    const auto migrations_dir = make_migrations_dir(workspace);
    const auto database_path = workspace.path() / "blogalone.db";
    const auto migration_path = migrations_dir / "001_init.sql";
    write_file(
        migration_path,
        std::string{kSchemaMigrationsSql} + "CREATE TABLE users(id INTEGER PRIMARY KEY);\n"
    );

    ASSERT_EQ(
        blogalone::db::run_migrations({
            .database_path = database_path,
            .migrations_dir = migrations_dir
        }).size(),
        1
    );
    write_file(
        migration_path,
        std::string{kSchemaMigrationsSql}
            + "CREATE TABLE users(id INTEGER PRIMARY KEY);\n"
            + "CREATE TABLE changed(id INTEGER PRIMARY KEY);\n"
    );

    EXPECT_THROW(
        static_cast<void>(blogalone::db::run_migrations({
            .database_path = database_path,
            .migrations_dir = migrations_dir
        })),
        blogalone::db::MigrationError
    );
}

TEST(MigrationRunnerTest, EnablesWalModeOnStartupConnection)
{
    TempWorkspace workspace;
    const auto migrations_dir = make_migrations_dir(workspace);
    const auto database_path = workspace.path() / "blogalone.db";
    write_file(
        migrations_dir / "001_init.sql",
        std::string{kSchemaMigrationsSql} + "CREATE TABLE users(id INTEGER PRIMARY KEY);\n"
    );

    static_cast<void>(blogalone::db::run_migrations({
        .database_path = database_path,
        .migrations_dir = migrations_dir
    }));

    TestDatabase database{database_path};
    EXPECT_EQ(database.scalar_text("PRAGMA journal_mode"), "wal");
}

TEST(MigrationRunnerTest, AppliesProjectInitialMigration)
{
    TempWorkspace workspace;
    const auto database_path = workspace.path() / "blogalone.db";
    const auto migrations_dir = std::filesystem::path{BLOGALONE_SOURCE_DIR} / "migrations";

    const auto applied = blogalone::db::run_migrations({
        .database_path = database_path,
        .migrations_dir = migrations_dir
    });

    TestDatabase database{database_path};
    EXPECT_EQ(applied.size(), 2);
    EXPECT_TRUE(database.table_exists("users"));
    EXPECT_TRUE(database.table_exists("threads"));
    EXPECT_TRUE(database.table_exists("schema_migrations"));
    EXPECT_EQ(
        database.scalar_int(
            "SELECT COUNT(*) FROM pragma_table_info('uploads') WHERE name = 'pending_delete_at'"
        ),
        1
    );
    EXPECT_EQ(database.scalar_int("SELECT COUNT(*) FROM schema_migrations"), 2);
}
