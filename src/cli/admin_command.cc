#include "cli/admin_command.h"

#include "util/credentials.h"
#include "util/password.h"
#include "util/time.h"

#include <sqlite3.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

namespace blogalone::cli {
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

using SqlitePtr = std::unique_ptr<sqlite3, SqliteDeleter>;
using StatementPtr = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

[[noreturn]] void throw_sqlite(sqlite3* database, std::string_view action)
{
    throw AdminCommandError{std::string{action} + ": " + sqlite3_errmsg(database)};
}

class Statement {
  public:
    Statement(sqlite3* database, std::string_view sql)
        : database_{database}
    {
        sqlite3_stmt* statement = nullptr;
        const std::string sql_text{sql};
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
            throw AdminCommandError{"expected SQL statement to finish without rows"};
        }
    }

    [[nodiscard]] std::int64_t column_int64(int index) const
    {
        return sqlite3_column_int64(statement_.get(), index);
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
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
            nullptr
        );
        database_.reset(database);
        if(result != SQLITE_OK) {
            const auto message = database_ == nullptr
                ? std::string{"open SQLite database: unable to allocate handle"}
                : std::string{"open SQLite database: "} + sqlite3_errmsg(database_.get());
            throw AdminCommandError{message};
        }
        if(sqlite3_busy_timeout(database_.get(), 5'000) != SQLITE_OK) {
            throw_sqlite(database_.get(), "set SQLite busy timeout");
        }
        execute("PRAGMA foreign_keys = ON;");
    }

    void execute(std::string_view sql)
    {
        const std::string sql_text{sql};
        if(sqlite3_exec(database_.get(), sql_text.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw_sqlite(database_.get(), "execute SQL");
        }
    }

    [[nodiscard]] sqlite3* get() const
    {
        return database_.get();
    }

    [[nodiscard]] std::int64_t last_insert_id() const
    {
        return sqlite3_last_insert_rowid(database_.get());
    }

  private:
    SqlitePtr database_;
};

[[nodiscard]] std::string read_password(const std::filesystem::path& path)
{
    std::ifstream input{path, std::ios::binary};
    if(!input) {
        throw AdminCommandError{"unable to read password file: " + path.string()};
    }

    std::string password{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    if(password.ends_with('\n')) {
        password.pop_back();
        if(password.ends_with('\r')) {
            password.pop_back();
        }
    }
    if(!util::is_valid_password(password)) {
        throw AdminCommandError{"password must contain 8 to 128 bytes"};
    }
    return password;
}

[[nodiscard]] std::int64_t admin_count(sqlite3* database)
{
    Statement statement{database, "SELECT COUNT(*) FROM users WHERE role = 'admin'"};
    if(!statement.step_row()) {
        throw AdminCommandError{"unable to count administrators"};
    }
    return statement.column_int64(0);
}

[[nodiscard]] bool username_exists(sqlite3* database, std::string_view username)
{
    Statement statement{
        database,
        "SELECT 1 FROM users WHERE username = ? COLLATE NOCASE LIMIT 1"
    };
    statement.bind(1, username);
    return statement.step_row();
}

[[nodiscard]] std::int64_t insert_admin(
    SqliteConnection& connection,
    std::string_view username,
    std::string_view password_hash,
    std::int64_t now
)
{
    Statement statement{
        connection.get(),
        "INSERT INTO users (username, email, pwd_hash, role, created_at, updated_at) "
        "VALUES (?, NULL, ?, 'admin', ?, ?)"
    };
    statement.bind(1, username);
    statement.bind(2, password_hash);
    statement.bind(3, now);
    statement.bind(4, now);
    statement.step_done();
    return connection.last_insert_id();
}

void insert_force_audit(SqliteConnection& connection, std::int64_t user_id, std::int64_t now)
{
    Statement statement{
        connection.get(),
        "INSERT INTO audit_log "
        "(admin_id, action, target_type, target_id, detail, created_at) "
        "VALUES (NULL, 'admin.bootstrap_force', 'user', ?, "
        "'{\"source\":\"local_cli\",\"forced\":true}', ?)"
    };
    statement.bind(1, user_id);
    statement.bind(2, now);
    statement.step_done();
}

}

std::int64_t create_admin(const AdminCreateOptions& options)
{
    if(options.database_path.empty()) {
        throw AdminCommandError{"database path is required"};
    }
    if(!util::is_valid_username(options.username)) {
        throw AdminCommandError{"username must contain 3 to 32 letters, digits, underscores, or Chinese characters"};
    }

    const auto password = read_password(options.password_file);
    const auto password_hash = util::hash_password(password, options.password_hash_options);
    SqliteConnection connection{options.database_path};
    connection.execute("BEGIN IMMEDIATE;");

    try {
        if(admin_count(connection.get()) > 0 && !options.force) {
            throw AdminCommandError{"an administrator already exists; use --force to create another"};
        }
        if(username_exists(connection.get(), options.username)) {
            throw AdminCommandError{"username already exists"};
        }

        const auto now = util::utc_unix_seconds();
        const auto user_id = insert_admin(connection, options.username, password_hash, now);
        if(options.force) {
            insert_force_audit(connection, user_id, now);
        }
        connection.execute("COMMIT;");
        return user_id;
    } catch(...) {
        try {
            connection.execute("ROLLBACK;");
        } catch(const AdminCommandError&) {
        }
        throw;
    }
}

}
