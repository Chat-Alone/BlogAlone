#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace blogalone::util {

inline constexpr std::size_t kMaxDecodedImageBytes = 128 * 1024 * 1024;
inline constexpr std::int64_t kMaxDecodedImageDimension = 5792;

static_assert(
    static_cast<std::size_t>(kMaxDecodedImageDimension) * static_cast<std::size_t>(kMaxDecodedImageDimension)
        <= kMaxDecodedImageBytes / 4
);

enum class ImageFormat {
    png,
    jpeg,
    gif,
    webp
};

struct ImageInfo {
    ImageFormat format{};
    std::uint32_t width{};
    std::uint32_t height{};
};

// Detects the image format and pixel dimensions purely from the byte content,
// ignoring any client-supplied MIME or extension. Returns nullopt when the data
// does not match a supported image header or is truncated.
[[nodiscard]] std::optional<ImageInfo> probe_image(std::span<const unsigned char> data);

// Attempts to fully decode the image data using the appropriate library for each
// format. Returns true only when the entire image can be decoded without error.
// Use this as a second-pass validation after probe_image succeeds.
[[nodiscard]] bool validate_image_decode(std::span<const unsigned char> data, ImageFormat format);

[[nodiscard]] std::string_view mime_for(ImageFormat format);
[[nodiscard]] std::string_view extension_for(ImageFormat format);

}
