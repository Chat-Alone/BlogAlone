#include "services/upload_service.h"

#include "db/transaction.h"
#include "services/upload_file_mutex.h"
#include "util/crypto.h"
#include "util/image.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

namespace blogalone::services {
namespace {

[[nodiscard]] std::string year_month_prefix(std::int64_t now)
{
    const std::chrono::sys_seconds point{std::chrono::seconds{now}};
    const auto days = std::chrono::floor<std::chrono::days>(point);
    const std::chrono::year_month_day ymd{days};
    const int year = static_cast<int>(ymd.year());
    const unsigned month = static_cast<unsigned>(ymd.month());

    std::string prefix;
    prefix.resize(7);
    prefix[0] = static_cast<char>('0' + (year / 1000) % 10);
    prefix[1] = static_cast<char>('0' + (year / 100) % 10);
    prefix[2] = static_cast<char>('0' + (year / 10) % 10);
    prefix[3] = static_cast<char>('0' + year % 10);
    prefix[4] = '/';
    prefix[5] = static_cast<char>('0' + (month / 10) % 10);
    prefix[6] = static_cast<char>('0' + month % 10);
    return prefix;
}

[[nodiscard]] bool write_file(const std::filesystem::path& path, std::string_view content)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if(error) {
        return false;
    }
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    if(!stream) {
        return false;
    }
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return stream.good();
}

void remove_created_file_if_unreferenced(
    const repositories::UploadRepository& repository,
    std::string_view sha256,
    std::string_view relative_path,
    const std::filesystem::path& absolute,
    bool created_file
)
{
    if(!created_file) {
        return;
    }
    try {
        const auto existing = repository.find_by_sha256(sha256);
        if(existing.has_value() && existing->path == relative_path) {
            return;
        }
    } catch(...) {
        return;
    }

    std::error_code error;
    std::filesystem::remove(absolute, error);
}

}

std::string_view to_string(UploadError error)
{
    switch(error) {
    case UploadError::invalid_input:
        return "invalid_input";
    case UploadError::unsupported_type:
        return "unsupported_type";
    case UploadError::too_large:
        return "too_large";
    case UploadError::rate_limited:
        return "rate_limited";
    case UploadError::not_found:
        return "not_found";
    case UploadError::forbidden:
        return "forbidden";
    case UploadError::internal_error:
        return "internal_error";
    }
    return "internal_error";
}

UploadService::UploadService(
    std::filesystem::path uploads_root,
    UploadLimits limits,
    repositories::UploadRepository upload_repository,
    repositories::UserRepository user_repository
)
    : uploads_root_{std::move(uploads_root)}
    , limits_{limits}
    , upload_repository_{std::move(upload_repository)}
    , user_repository_{std::move(user_repository)}
{
}

UploadResult<UploadDescriptor> UploadService::store_image(
    std::int64_t owner_id,
    std::string_view content,
    std::int64_t now
) const
{
    if(owner_id <= 0 || content.empty()) {
        return UploadError::invalid_input;
    }
    if(static_cast<std::int64_t>(content.size()) > limits_.max_file_size) {
        return UploadError::too_large;
    }

    const auto owner = user_repository_.find_by_id(owner_id);
    if(!owner.has_value()) {
        return UploadError::not_found;
    }
    if(owner->banned_until.has_value() && *owner->banned_until > now) {
        return UploadError::forbidden;
    }

    const std::span<const unsigned char> bytes{
        reinterpret_cast<const unsigned char*>(content.data()),
        content.size()
    };
    const auto image = util::probe_image(bytes);
    if(!image.has_value()) {
        return UploadError::unsupported_type;
    }
    if(image->width == 0 || image->height == 0
        || image->width > static_cast<std::uint32_t>(limits_.max_dimension)
        || image->height > static_cast<std::uint32_t>(limits_.max_dimension)) {
        return UploadError::unsupported_type;
    }
    if(!util::validate_image_decode(bytes, image->format)) {
        return UploadError::unsupported_type;
    }

    const auto quota_since = now - 86'400;
    if(upload_repository_.count_owner_refs_since(owner_id, quota_since) >= limits_.max_daily_uploads) {
        return UploadError::rate_limited;
    }

    const auto sha256 = util::sha256_hex(content);
    const auto mime = util::mime_for(image->format);

    std::int64_t upload_id = 0;
    std::string relative_path;
    relative_path = year_month_prefix(now) + "/" + sha256.substr(0, 2) + "/" + sha256
        + "." + std::string{util::extension_for(image->format)};
    const auto absolute = uploads_root_ / std::filesystem::path{relative_path};
    const std::scoped_lock file_lock{upload_file_mutex()};
    std::error_code fs_error;
    const bool file_existed = std::filesystem::is_regular_file(absolute, fs_error);
    if(!file_existed && !write_file(absolute, content)) {
        return UploadError::internal_error;
    }
    const bool created_file = !file_existed;

    std::optional<UploadError> db_error;
    try {
        db::Transaction transaction{
            upload_repository_.client(),
            drogon::orm::TransactionType::Deferred
        };
        const repositories::UploadRepository repository{transaction.client()};
        if(repository.count_owner_refs_since(owner_id, quota_since) >= limits_.max_daily_uploads) {
            db_error = UploadError::rate_limited;
        } else {
            const auto inserted = repository.create_upload(
                sha256,
                relative_path,
                mime,
                static_cast<std::int64_t>(content.size()),
                static_cast<std::int64_t>(image->width),
                static_cast<std::int64_t>(image->height),
                now
            );
            if(inserted.has_value()) {
                upload_id = *inserted;
            } else {
                const auto existing = repository.find_by_sha256(sha256);
                if(existing.has_value() && !existing->pending_delete_at.has_value()) {
                    upload_id = existing->id;
                    relative_path = existing->path;
                } else {
                    db_error = UploadError::internal_error;
                }
            }
            if(!db_error.has_value()) {
                const auto created_ref = repository.create_ref(owner_id, upload_id, now);
                if(!created_ref && !repository.owner_has_upload_path(owner_id, relative_path)) {
                    db_error = UploadError::internal_error;
                }
            }
        }
        if(!db_error.has_value()) {
            transaction.commit();
        }
    } catch(...) {
        remove_created_file_if_unreferenced(
            upload_repository_,
            sha256,
            relative_path,
            absolute,
            created_file
        );
        throw;
    }

    if(db_error.has_value()) {
        remove_created_file_if_unreferenced(
            upload_repository_,
            sha256,
            relative_path,
            absolute,
            created_file
        );
        return *db_error;
    }

    return UploadDescriptor{
        .url = "/uploads/" + relative_path,
        .mime = std::string{mime},
        .size = static_cast<std::int64_t>(content.size()),
        .width = static_cast<std::int64_t>(image->width),
        .height = static_cast<std::int64_t>(image->height)
    };
}

}
