#pragma once

#include "repositories/upload_repository.h"

#include <cstdint>
#include <filesystem>

namespace blogalone::services {

struct UploadCleanupResult {
    std::int64_t refs_deleted{};
    std::int64_t uploads_marked{};
    std::int64_t uploads_deleted{};
    std::int64_t files_deleted{};
    std::int64_t untracked_files_deleted{};
    std::int64_t file_failures{};
};

class UploadCleanupService {
  public:
    explicit UploadCleanupService(
        std::filesystem::path uploads_root,
        repositories::UploadRepository upload_repository = repositories::UploadRepository{}
    );

    [[nodiscard]] UploadCleanupResult remove_orphans(
        std::int64_t cutoff,
        std::int64_t now
    ) const;

  private:
    std::filesystem::path uploads_root_;
    repositories::UploadRepository upload_repository_;
};

}
