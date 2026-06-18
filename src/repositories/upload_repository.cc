#include "repositories/upload_repository.h"

#include <drogon/drogon.h>

#include <utility>

namespace blogalone::repositories {
namespace {

[[nodiscard]] models::Upload row_to_upload(const drogon::orm::Row& row)
{
    return models::Upload{
        .id = row["id"].as<std::int64_t>(),
        .sha256 = row["sha256"].as<std::string>(),
        .path = row["path"].as<std::string>(),
        .mime = row["mime"].as<std::string>(),
        .size = row["size"].as<std::int64_t>(),
        .width = row["width"].isNull() ? std::nullopt : std::optional{row["width"].as<std::int64_t>()},
        .height = row["height"].isNull() ? std::nullopt : std::optional{row["height"].as<std::int64_t>()},
        .created_at = row["created_at"].as<std::int64_t>()
    };
}

constexpr const char* kSelectColumns =
    "SELECT id, sha256, path, mime, size, width, height, created_at FROM uploads";

}

UploadRepository::UploadRepository(std::string db_client_name)
    : db_client_name_{std::move(db_client_name)}
{
}

UploadRepository::UploadRepository(drogon::orm::DbClientPtr db_client)
    : db_client_{std::move(db_client)}
{
}

drogon::orm::DbClientPtr UploadRepository::client() const
{
    if(db_client_) {
        return db_client_;
    }
    return drogon::app().getDbClient(db_client_name_);
}

std::optional<models::Upload> UploadRepository::find_by_sha256(std::string_view sha256) const
{
    const auto db = client();
    const auto rows = db->execSqlSync(
        std::string{kSelectColumns} + " WHERE sha256 = ?",
        std::string{sha256}
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return row_to_upload(rows.at(0));
}

std::optional<std::int64_t> UploadRepository::create_upload(
    std::string_view sha256,
    std::string_view path,
    std::string_view mime,
    std::int64_t size,
    std::optional<std::int64_t> width,
    std::optional<std::int64_t> height,
    std::int64_t created_at
) const
{
    const auto db = client();
    const auto rows = db->execSqlSync(
        "INSERT OR IGNORE INTO uploads (sha256, path, mime, size, width, height, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id",
        std::string{sha256},
        std::string{path},
        std::string{mime},
        size,
        width,
        height,
        created_at
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return rows.at(0)["id"].as<std::int64_t>();
}

bool UploadRepository::create_ref(
    std::int64_t owner_id,
    std::int64_t upload_id,
    std::int64_t created_at
) const
{
    const auto db = client();
    const auto rows = db->execSqlSync(
        "INSERT OR IGNORE INTO upload_refs (owner_id, upload_id, created_at) "
        "VALUES (?, ?, ?) RETURNING id",
        owner_id,
        upload_id,
        created_at
    );
    return !rows.empty();
}

std::int64_t UploadRepository::count_owner_refs_since(std::int64_t owner_id, std::int64_t since) const
{
    const auto db = client();
    const auto rows = db->execSqlSync(
        "SELECT COUNT(*) AS total FROM upload_refs WHERE owner_id = ? AND created_at >= ?",
        owner_id,
        since
    );
    return rows.at(0)["total"].as<std::int64_t>();
}

bool UploadRepository::owner_has_upload_path(std::int64_t owner_id, std::string_view path) const
{
    const auto db = client();
    const auto rows = db->execSqlSync(
        "SELECT 1 FROM upload_refs r JOIN uploads u ON u.id = r.upload_id "
        "WHERE r.owner_id = ? AND u.path = ?",
        owner_id,
        std::string{path}
    );
    return !rows.empty();
}

void UploadRepository::mark_ref_attached(
    std::int64_t owner_id,
    std::string_view path,
    std::int64_t attached_at
) const
{
    const auto db = client();
    db->execSqlSync(
        "UPDATE upload_refs SET attached_at = ? "
        "WHERE attached_at IS NULL AND owner_id = ? AND upload_id IN "
        "(SELECT id FROM uploads WHERE path = ?)",
        attached_at,
        owner_id,
        std::string{path}
    );
}

}
