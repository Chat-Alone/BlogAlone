#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace blogalone::util {

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

[[nodiscard]] std::string_view mime_for(ImageFormat format);
[[nodiscard]] std::string_view extension_for(ImageFormat format);

}
