#include "services/upload_cleanup_service.h"

#include "db/transaction.h"
#include "services/upload_file_mutex.h"
#include "util/crypto.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace blogalone::services {
namespace {

[[nodiscard]] bool escapes_root(const std::filesystem::path& relative)
{
    if(relative.empty() || relative.is_absolute() || relative.has_root_name()) {
        return true;
    }
    for(const auto& component : relative) {
        if(component == "." || component == "..") {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<std::filesystem::path> contained_path(
    const std::filesystem::path& root,
    const std::string& relative_path
)
{
    const std::filesystem::path relative{relative_path};
    if(escapes_root(relative)) {
        return std::nullopt;
    }

    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if(error) {
        return std::nullopt;
    }
    const auto candidate = std::filesystem::weakly_canonical(canonical_root / relative, error);
    if(error) {
        return std::nullopt;
    }
    const auto contained = candidate.lexically_relative(canonical_root);
    if(contained.empty() || contained.is_absolute()) {
        return std::nullopt;
    }
    const auto first = contained.begin();
    if(first != contained.end() && *first == "..") {
        return std::nullopt;
    }
    return candidate;
}

[[nodiscard]] bool remove_upload_file(
    const std::filesystem::path& root,
    const std::string& relative_path,
    bool& existed
)
{
    const auto path = contained_path(root, relative_path);
    if(!path.has_value()) {
        existed = true;
        return false;
    }

    std::error_code error;
    existed = std::filesystem::exists(*path, error);
    if(error) {
        return false;
    }
    if(!existed) {
        return true;
    }
    if(!std::filesystem::is_regular_file(*path, error) || error) {
        return false;
    }
    return std::filesystem::remove(*path, error) && !error;
}

[[nodiscard]] bool is_ascii_digits(std::string_view value)
{
    return std::ranges::all_of(value, [](char ch) {
        return ch >= '0' && ch <= '9';
    });
}

[[nodiscard]] bool is_managed_upload_path(const std::filesystem::path& relative)
{
    std::vector<std::string> components;
    components.reserve(4);
    for(const auto& component : relative) {
        components.push_back(component.string());
    }
    if(components.size() != 4) {
        return false;
    }

    const auto& year = components.at(0);
    const auto& month = components.at(1);
    const auto& prefix = components.at(2);
    const std::filesystem::path filename{components.at(3)};
    const auto sha256 = filename.stem().string();
    const auto extension = filename.extension().string();
    const auto known_extension = extension == ".jpg" || extension == ".png"
        || extension == ".gif" || extension == ".webp";

    return year.size() == 4 && is_ascii_digits(year)
        && month.size() == 2 && is_ascii_digits(month) && month >= "01" && month <= "12"
        && prefix.size() == 2 && util::is_lower_hex(prefix)
        && sha256.size() == 64 && util::is_lower_hex(sha256)
        && sha256.starts_with(prefix) && known_extension;
}

void prepare_pending_uploads(
    const repositories::UploadRepository& upload_repository,
    std::int64_t cutoff,
    std::int64_t now,
    UploadCleanupResult& result
)
{
    db::Transaction transaction{
        upload_repository.client(),
        drogon::orm::TransactionType::Immediate
    };
    const repositories::UploadRepository repository{transaction.client()};
    result.refs_deleted = repository.delete_unattached_refs_before(cutoff);
    result.uploads_marked = repository.mark_unreferenced_uploads_pending(now);
    transaction.commit();
}

void remove_pending_uploads(
    const std::filesystem::path& uploads_root,
    const repositories::UploadRepository& upload_repository,
    UploadCleanupResult& result
)
{
    const auto pending_uploads = upload_repository.list_pending_uploads();
    for(const auto& upload : pending_uploads) {
        const std::scoped_lock file_lock{upload_file_mutex()};
        bool existed = false;
        if(!remove_upload_file(uploads_root, upload.path, existed)) {
            ++result.file_failures;
            continue;
        }
        if(existed) {
            ++result.files_deleted;
        }
        if(upload_repository.delete_pending_upload(upload.id)) {
            ++result.uploads_deleted;
        }
    }
}

void remove_untracked_upload_files(
    const std::filesystem::path& uploads_root,
    const repositories::UploadRepository& upload_repository,
    UploadCleanupResult& result
)
{
    const std::scoped_lock file_lock{upload_file_mutex()};
    const auto tracked_paths = upload_repository.list_tracked_upload_paths();
    const std::unordered_set<std::string> tracked{tracked_paths.begin(), tracked_paths.end()};

    std::error_code error;
    if(!std::filesystem::exists(uploads_root, error)) {
        if(error) {
            ++result.file_failures;
        }
        return;
    }

    std::filesystem::recursive_directory_iterator iterator{
        uploads_root,
        std::filesystem::directory_options::skip_permission_denied,
        error
    };
    const std::filesystem::recursive_directory_iterator end;
    if(error) {
        ++result.file_failures;
        return;
    }

    while(iterator != end) {
        const auto entry = *iterator;
        iterator.increment(error);
        if(error) {
            ++result.file_failures;
            break;
        }

        if(!entry.is_regular_file(error)) {
            if(error) {
                ++result.file_failures;
                error.clear();
            }
            continue;
        }
        const auto relative = std::filesystem::relative(entry.path(), uploads_root, error);
        if(error) {
            ++result.file_failures;
            error.clear();
            continue;
        }
        const auto relative_path = relative.generic_string();
        if(!is_managed_upload_path(relative) || tracked.contains(relative_path)) {
            continue;
        }

        bool existed = false;
        if(remove_upload_file(uploads_root, relative_path, existed)) {
            if(existed) {
                ++result.files_deleted;
                ++result.untracked_files_deleted;
            }
        } else {
            ++result.file_failures;
        }
    }
}

}

UploadCleanupService::UploadCleanupService(
    std::filesystem::path uploads_root,
    repositories::UploadRepository upload_repository
)
    : uploads_root_{std::move(uploads_root)}
    , upload_repository_{std::move(upload_repository)}
{
}

UploadCleanupResult UploadCleanupService::remove_orphans(
    std::int64_t cutoff,
    std::int64_t now
) const
{
    UploadCleanupResult result;
    prepare_pending_uploads(upload_repository_, cutoff, now, result);
    remove_pending_uploads(uploads_root_, upload_repository_, result);
    remove_untracked_upload_files(uploads_root_, upload_repository_, result);
    return result;
}

}
