#include "db/migrations.h"

#include "util/crypto.h"
#include "util/time.h"

#include <sodium.h>
#include <sqlite3.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace blogalone::db {
namespace {

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

struct SqliteErrorDeleter {
    void operator()(char* message) const noexcept
    {
        sqlite3_free(message);
    }
};

using SqlitePtr = std::unique_ptr<sqlite3, SqliteDeleter>;
using StatementPtr = std::unique_ptr<sqlite3_stmt, StatementDeleter>;
using SqliteErrorPtr = std::unique_ptr<char, SqliteErrorDeleter>;

[[nodiscard]] std::string sqlite_error(sqlite3* database, std::string_view action)
{
    std::string message{action};
    message += ": ";
    message += sqlite3_errmsg(database);
    return message;
}

[[noreturn]] void throw_sqlite(sqlite3* database, std::string_view action)
{
    throw MigrationError{sqlite_error(database, action)};
}

class Statement {
  public:
    Statement(sqlite3* database, std::string_view sql)
        : database_{database}
    {
        sqlite3_stmt* statement = nullptr;
        const auto sql_text = std::string{sql};
        const auto result = sqlite3_prepare_v2(
            database_,
            sql_text.c_str(),
            -1,
            &statement,
            nullptr
        );
        statement_.reset(statement);

        if(result != SQLITE_OK) {
            throw_sqlite(database_, "prepare SQL");
        }
    }

    void bind(int index, int value)
    {
        if(sqlite3_bind_int(statement_.get(), index, value) != SQLITE_OK) {
            throw_sqlite(database_, "bind integer");
        }
    }

    void bind(int index, std::int64_t value)
    {
        if(sqlite3_bind_int64(statement_.get(), index, value) != SQLITE_OK) {
            throw_sqlite(database_, "bind integer");
        }
    }

    void bind(int index, std::string_view value)
    {
        const auto result = sqlite3_bind_text(
            statement_.get(),
            index,
            value.data(),
            static_cast<int>(value.size()),
            SQLITE_TRANSIENT
        );
        if(result != SQLITE_OK) {
            throw_sqlite(database_, "bind text");
        }
    }

    [[nodiscard]] bool step_row()
    {
        const auto result = sqlite3_step(statement_.get());
        if(result == SQLITE_ROW) {
            return true;
        }
        if(result == SQLITE_DONE) {
            return false;
        }
        throw_sqlite(database_, "step SQL");
    }

    void step_done()
    {
        if(step_row()) {
            throw MigrationError{"expected SQL statement to finish without rows"};
        }
    }

    [[nodiscard]] int column_int(int index) const
    {
        return sqlite3_column_int(statement_.get(), index);
    }

    [[nodiscard]] std::string column_text(int index) const
    {
        const auto* text = sqlite3_column_text(statement_.get(), index);
        if(text == nullptr) {
            return {};
        }
        return reinterpret_cast<const char*>(text);
    }

  private:
    sqlite3* database_;
    StatementPtr statement_;
};

class SqliteConnection {
  public:
    explicit SqliteConnection(const std::filesystem::path& database_path)
    {
        sqlite3* database = nullptr;
        const auto result = sqlite3_open_v2(
            database_path.string().c_str(),
            &database,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
            nullptr
        );
        database_.reset(database);

        if(result != SQLITE_OK) {
            const auto message = database_ == nullptr
                ? std::string{"open SQLite database: unable to allocate handle"}
                : sqlite_error(database_.get(), "open SQLite database");
            throw MigrationError{message};
        }
    }

    void execute(std::string_view sql)
    {
        char* raw_message = nullptr;
        const auto sql_text = std::string{sql};
        const auto result = sqlite3_exec(
            database_.get(),
            sql_text.c_str(),
            nullptr,
            nullptr,
            &raw_message
        );
        SqliteErrorPtr message{raw_message};

        if(result != SQLITE_OK) {
            std::string error{"execute SQL: "};
            error += message == nullptr ? sqlite3_errmsg(database_.get()) : message.get();
            throw MigrationError{error};
        }
    }

    [[nodiscard]] bool table_exists(std::string_view table_name)
    {
        Statement statement{
            database_.get(),
            "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ? LIMIT 1"
        };
        statement.bind(1, table_name);
        return statement.step_row();
    }

    [[nodiscard]] std::map<int, std::string> applied_migrations()
    {
        std::map<int, std::string> applied;
        if(!table_exists("schema_migrations")) {
            return applied;
        }

        Statement statement{
            database_.get(),
            "SELECT version, checksum FROM schema_migrations ORDER BY version"
        };
        while(statement.step_row()) {
            applied.emplace(statement.column_int(0), statement.column_text(1));
        }
        return applied;
    }

    void insert_applied_migration(int version, std::string_view checksum, std::int64_t applied_at)
    {
        Statement statement{
            database_.get(),
            "INSERT INTO schema_migrations(version, checksum, applied_at) VALUES(?, ?, ?)"
        };
        statement.bind(1, version);
        statement.bind(2, checksum);
        statement.bind(3, applied_at);
        statement.step_done();
    }

  private:
    SqlitePtr database_;
};

struct MigrationFile {
    int version{};
    std::filesystem::path path;
    std::string sql;
    std::string checksum;
};

[[nodiscard]] std::optional<int> parse_version(const std::filesystem::path& path)
{
    const auto filename = path.filename().string();
    const auto separator = filename.find('_');
    if(separator == std::string::npos || separator == 0) {
        return std::nullopt;
    }

    const auto version_text = std::string_view{filename}.substr(0, separator);
    int version = 0;
    const auto* begin = version_text.data();
    const auto* end = begin + version_text.size();
    const auto [parsed, error] = std::from_chars(begin, end, version);
    if(error != std::errc{} || parsed != end || version <= 0) {
        return std::nullopt;
    }
    return version;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    if(!file) {
        throw MigrationError{"unable to read migration file: " + path.string()};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

[[nodiscard]] bool is_blank(std::string_view value)
{
    for(const auto ch : value) {
        if(ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<MigrationFile> load_migrations(const std::filesystem::path& migrations_dir)
{
    if(!std::filesystem::is_directory(migrations_dir)) {
        throw MigrationError{"migration directory does not exist: " + migrations_dir.string()};
    }

    std::vector<MigrationFile> migrations;
    for(const auto& entry : std::filesystem::directory_iterator{migrations_dir}) {
        if(!entry.is_regular_file() || entry.path().extension() != ".sql") {
            continue;
        }

        const auto version = parse_version(entry.path());
        if(!version) {
            continue;
        }

        auto sql = read_file(entry.path());
        if(is_blank(sql)) {
            throw MigrationError{"migration file is empty: " + entry.path().string()};
        }

        const auto checksum = util::sha256_hex(sql);
        migrations.push_back(MigrationFile{
            .version = *version,
            .path = entry.path(),
            .sql = std::move(sql),
            .checksum = checksum
        });
    }

    std::ranges::sort(
        migrations,
        [](const MigrationFile& left, const MigrationFile& right) {
            return left.version < right.version;
        }
    );

    for(std::size_t index = 1; index < migrations.size(); ++index) {
        if(migrations.at(index - 1).version == migrations.at(index).version) {
            throw MigrationError{
                "duplicate migration version: " + std::to_string(migrations.at(index).version)
            };
        }
    }

    return migrations;
}

void configure_sqlite(SqliteConnection& connection)
{
    connection.execute("PRAGMA foreign_keys = ON;");
    connection.execute("PRAGMA journal_mode = WAL;");
    connection.execute("PRAGMA synchronous = NORMAL;");
    connection.execute("PRAGMA busy_timeout = 5000;");
}

void apply_migration(SqliteConnection& connection, const MigrationFile& migration)
{
    connection.execute("BEGIN IMMEDIATE;");
    try {
        connection.execute(migration.sql);
        connection.insert_applied_migration(
            migration.version,
            migration.checksum,
            util::utc_unix_seconds()
        );
        connection.execute("COMMIT;");
    } catch(...) {
        try {
            connection.execute("ROLLBACK;");
        } catch(const MigrationError&) {
        }
        throw;
    }
}

}

std::vector<AppliedMigration> run_migrations(const MigrationOptions& options)
{
    if(options.database_path.empty()) {
        throw MigrationError{"database path is required"};
    }
    if(options.migrations_dir.empty()) {
        throw MigrationError{"migration directory is required"};
    }

    auto migrations = load_migrations(options.migrations_dir);
    SqliteConnection connection{options.database_path};
    configure_sqlite(connection);

    const auto applied = connection.applied_migrations();
    std::vector<AppliedMigration> applied_now;
    for(const auto& migration : migrations) {
        const auto existing = applied.find(migration.version);
        if(existing != applied.end()) {
            if(existing->second != migration.checksum) {
                throw MigrationError{
                    "migration checksum changed for version " + std::to_string(migration.version)
                };
            }
            continue;
        }

        apply_migration(connection, migration);
        applied_now.push_back(AppliedMigration{
            .version = migration.version,
            .path = migration.path,
            .checksum = migration.checksum
        });
    }

    return applied_now;
}

}
