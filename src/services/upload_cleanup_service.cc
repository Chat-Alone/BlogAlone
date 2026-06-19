#include "services/upload_cleanup_service.h"

#include <optional>
#include <string>
#include <system_error>
#include <utility>

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

}

UploadCleanupService::UploadCleanupService(
    std::filesystem::path uploads_root,
    repositories::UploadRepository upload_repository
)
    : uploads_root_{std::move(uploads_root)}
    , upload_repository_{std::move(upload_repository)}
{
}

UploadCleanupResult UploadCleanupService::remove_orphans(std::int64_t cutoff) const
{
    UploadCleanupResult result;
    result.refs_deleted = upload_repository_.delete_unattached_refs_before(cutoff);
    const auto uploads = upload_repository_.delete_unreferenced_uploads();
    result.uploads_deleted = static_cast<std::int64_t>(uploads.size());

    for(const auto& upload : uploads) {
        bool existed = false;
        if(remove_upload_file(uploads_root_, upload.path, existed)) {
            if(existed) {
                ++result.files_deleted;
            }
        } else {
            ++result.file_failures;
        }
    }
    return result;
}

}
