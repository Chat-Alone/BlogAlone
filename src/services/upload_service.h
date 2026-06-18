#pragma once

#include "repositories/upload_repository.h"
#include "repositories/user_repository.h"
#include "util/image.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

namespace blogalone::services {

struct UploadLimits {
    std::int64_t max_file_size{5 * 1024 * 1024};
    std::int64_t max_daily_uploads{100};
    std::int64_t max_dimension{util::kMaxDecodedImageDimension};
};

struct UploadDescriptor {
    std::string url;
    std::string mime;
    std::int64_t size{};
    std::int64_t width{};
    std::int64_t height{};
};

enum class UploadError {
    invalid_input,
    unsupported_type,
    too_large,
    rate_limited,
    not_found,
    forbidden,
    internal_error
};

[[nodiscard]] std::string_view to_string(UploadError error);

template <typename T>
class UploadResult {
  public:
    UploadResult(T value) : storage_{std::move(value)} {}
    UploadResult(UploadError error) : storage_{error} {}

    [[nodiscard]] bool has_value() const { return storage_.index() == 0; }
    [[nodiscard]] explicit operator bool() const { return has_value(); }
    [[nodiscard]] const T& value() const { return std::get<0>(storage_); }
    [[nodiscard]] UploadError error() const { return std::get<1>(storage_); }
    [[nodiscard]] const T& operator*() const { return value(); }
    [[nodiscard]] const T* operator->() const { return &value(); }

  private:
    std::variant<T, UploadError> storage_;
};

class UploadService {
  public:
    explicit UploadService(
        std::filesystem::path uploads_root,
        UploadLimits limits = UploadLimits{},
        repositories::UploadRepository upload_repository = repositories::UploadRepository{},
        repositories::UserRepository user_repository = repositories::UserRepository{}
    );

    [[nodiscard]] UploadResult<UploadDescriptor> store_image(
        std::int64_t owner_id,
        std::string_view content,
        std::int64_t now
    ) const;

  private:
    std::filesystem::path uploads_root_;
    UploadLimits limits_;
    repositories::UploadRepository upload_repository_;
    repositories::UserRepository user_repository_;
};

}
