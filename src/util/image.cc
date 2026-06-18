#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324 4611)
#endif

#include "util/image.h"

#include <jpeglib.h>
#include <spng.h>
#include <webp/decode.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <setjmp.h>
#include <vector>

namespace blogalone::util {
namespace {

constexpr std::size_t kMaxDecodedImageBytes = 128 * 1024 * 1024;

struct SpngContextDeleter {
    void operator()(spng_ctx* ctx) const { spng_ctx_free(ctx); }
};

using SpngContextPtr = std::unique_ptr<spng_ctx, SpngContextDeleter>;

[[nodiscard]] bool has_prefix(std::span<const unsigned char> data, std::span<const unsigned char> prefix)
{
    return data.size() >= prefix.size()
        && std::ranges::equal(data.first(prefix.size()), prefix);
}

[[nodiscard]] std::uint32_t read_be16(std::span<const unsigned char> data, std::size_t offset)
{
    return (static_cast<std::uint32_t>(data[offset]) << 8)
        | static_cast<std::uint32_t>(data[offset + 1]);
}

[[nodiscard]] std::uint32_t read_be32(std::span<const unsigned char> data, std::size_t offset)
{
    return (static_cast<std::uint32_t>(data[offset]) << 24)
        | (static_cast<std::uint32_t>(data[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 8)
        | static_cast<std::uint32_t>(data[offset + 3]);
}

[[nodiscard]] std::uint32_t read_le16(std::span<const unsigned char> data, std::size_t offset)
{
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8);
}

[[nodiscard]] std::uint32_t read_le24(std::span<const unsigned char> data, std::size_t offset)
{
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16);
}

[[nodiscard]] std::uint32_t read_le32(std::span<const unsigned char> data, std::size_t offset)
{
    return static_cast<std::uint32_t>(data[offset])
        | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

[[nodiscard]] std::optional<ImageInfo> probe_png(std::span<const unsigned char> data)
{
    constexpr std::array<unsigned char, 8> signature{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    if(!has_prefix(data, signature) || data.size() < 24) {
        return std::nullopt;
    }
    constexpr std::array<unsigned char, 4> ihdr{'I', 'H', 'D', 'R'};
    if(!std::ranges::equal(data.subspan(12, 4), ihdr)) {
        return std::nullopt;
    }
    return ImageInfo{
        .format = ImageFormat::png,
        .width = read_be32(data, 16),
        .height = read_be32(data, 20)
    };
}

[[nodiscard]] std::optional<ImageInfo> probe_gif(std::span<const unsigned char> data)
{
    constexpr std::array<unsigned char, 6> gif87{'G', 'I', 'F', '8', '7', 'a'};
    constexpr std::array<unsigned char, 6> gif89{'G', 'I', 'F', '8', '9', 'a'};
    if(data.size() < 10 || (!has_prefix(data, gif87) && !has_prefix(data, gif89))) {
        return std::nullopt;
    }
    return ImageInfo{
        .format = ImageFormat::gif,
        .width = read_le16(data, 6),
        .height = read_le16(data, 8)
    };
}

[[nodiscard]] bool is_sof_marker(unsigned char marker)
{
    if(marker < 0xc0 || marker > 0xcf) {
        return false;
    }
    return marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
}

[[nodiscard]] bool is_standalone_marker(unsigned char marker)
{
    return marker == 0xd8 || marker == 0xd9 || marker == 0x01
        || (marker >= 0xd0 && marker <= 0xd7);
}

[[nodiscard]] std::optional<ImageInfo> probe_jpeg(std::span<const unsigned char> data)
{
    if(data.size() < 4 || data[0] != 0xff || data[1] != 0xd8) {
        return std::nullopt;
    }

    std::size_t index = 2;
    while(index + 3 < data.size()) {
        if(data[index] != 0xff) {
            return std::nullopt;
        }
        while(index < data.size() && data[index] == 0xff) {
            ++index;
        }
        if(index >= data.size()) {
            return std::nullopt;
        }
        const auto marker = data[index];
        ++index;
        if(is_standalone_marker(marker)) {
            continue;
        }
        if(index + 1 >= data.size()) {
            return std::nullopt;
        }
        const auto segment_length = read_be16(data, index);
        if(segment_length < 2) {
            return std::nullopt;
        }
        if(is_sof_marker(marker)) {
            if(index + 6 >= data.size()) {
                return std::nullopt;
            }
            return ImageInfo{
                .format = ImageFormat::jpeg,
                .width = read_be16(data, index + 5),
                .height = read_be16(data, index + 3)
            };
        }
        index += segment_length;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ImageInfo> probe_webp(std::span<const unsigned char> data)
{
    constexpr std::array<unsigned char, 4> riff{'R', 'I', 'F', 'F'};
    constexpr std::array<unsigned char, 4> webp{'W', 'E', 'B', 'P'};
    if(data.size() < 16 || !has_prefix(data, riff) || !std::ranges::equal(data.subspan(8, 4), webp)) {
        return std::nullopt;
    }

    const auto fourcc = data.subspan(12, 4);
    if(std::ranges::equal(fourcc, std::array<unsigned char, 4>{'V', 'P', '8', ' '})) {
        if(data.size() < 30 || data[23] != 0x9d || data[24] != 0x01 || data[25] != 0x2a) {
            return std::nullopt;
        }
        return ImageInfo{
            .format = ImageFormat::webp,
            .width = read_le16(data, 26) & 0x3fff,
            .height = read_le16(data, 28) & 0x3fff
        };
    }
    if(std::ranges::equal(fourcc, std::array<unsigned char, 4>{'V', 'P', '8', 'L'})) {
        if(data.size() < 25 || data[20] != 0x2f) {
            return std::nullopt;
        }
        const auto bits = read_le32(data, 21);
        return ImageInfo{
            .format = ImageFormat::webp,
            .width = (bits & 0x3fff) + 1,
            .height = ((bits >> 14) & 0x3fff) + 1
        };
    }
    if(std::ranges::equal(fourcc, std::array<unsigned char, 4>{'V', 'P', '8', 'X'})) {
        if(data.size() < 30) {
            return std::nullopt;
        }
        return ImageInfo{
            .format = ImageFormat::webp,
            .width = read_le24(data, 24) + 1,
            .height = read_le24(data, 27) + 1
        };
    }
    return std::nullopt;
}

[[nodiscard]] bool validate_png_decode(std::span<const unsigned char> data)
{
    SpngContextPtr ctx{spng_ctx_new(0)};
    if(!ctx) {
        return false;
    }
    if(spng_set_png_buffer(ctx.get(), data.data(), data.size()) != SPNG_OK) {
        return false;
    }
    spng_ihdr ihdr{};
    if(spng_get_ihdr(ctx.get(), &ihdr) != SPNG_OK) {
        return false;
    }
    size_t image_size = 0;
    if(spng_decoded_image_size(ctx.get(), SPNG_FMT_RGBA8, &image_size) != SPNG_OK
        || image_size == 0 || image_size > kMaxDecodedImageBytes) {
        return false;
    }
    std::vector<unsigned char> output(image_size);
    return spng_decode_image(ctx.get(), output.data(), output.size(), SPNG_FMT_RGBA8, 0) == SPNG_OK;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324 4611)
#endif

struct JpegErrorManager {
    jpeg_error_mgr pub;
    jmp_buf jump_buffer;
};

[[noreturn]] void jpeg_error_exit(j_common_ptr cinfo)
{
    auto* err = reinterpret_cast<JpegErrorManager*>(cinfo->err);
    longjmp(err->jump_buffer, 1);
}

[[nodiscard]] bool validate_jpeg_decode(std::span<const unsigned char> data)
{
    JpegErrorManager error_mgr{};
    jpeg_decompress_struct cinfo{};
    cinfo.err = jpeg_std_error(&error_mgr.pub);
    error_mgr.pub.error_exit = jpeg_error_exit;

    if(setjmp(error_mgr.jump_buffer) != 0) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data.data(), static_cast<unsigned long>(data.size()));
    if(jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    if(!jpeg_start_decompress(&cinfo)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    const auto row_stride = static_cast<std::size_t>(cinfo.output_width)
        * static_cast<std::size_t>(cinfo.output_components);
    if(row_stride == 0 || row_stride > kMaxDecodedImageBytes) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    std::vector<unsigned char> scanline(row_stride);
    while(cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = scanline.data();
        if(jpeg_read_scanlines(&cinfo, &row, 1) != 1) {
            jpeg_destroy_decompress(&cinfo);
            return false;
        }
    }
    if(!jpeg_finish_decompress(&cinfo)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    jpeg_destroy_decompress(&cinfo);
    return true;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

[[nodiscard]] bool validate_webp_decode(std::span<const unsigned char> data)
{
    WebPDecoderConfig config;
    if(WebPInitDecoderConfig(&config) == 0) {
        return false;
    }
    if(WebPGetInfo(data.data(), data.size(), nullptr, nullptr) == 0) {
        WebPFreeDecBuffer(&config.output);
        return false;
    }
    const VP8StatusCode status = WebPDecode(data.data(), data.size(), &config);
    WebPFreeDecBuffer(&config.output);
    return status == VP8_STATUS_OK;
}

[[nodiscard]] bool skip_gif_color_table(
    std::span<const unsigned char> data,
    std::size_t& index,
    unsigned char packed
)
{
    if((packed & 0x80) == 0) {
        return true;
    }
    const auto color_table_size = 3ULL * (1ULL << ((packed & 0x07) + 1));
    if(color_table_size > data.size() - index) {
        return false;
    }
    index += static_cast<std::size_t>(color_table_size);
    return true;
}

[[nodiscard]] bool skip_gif_sub_blocks(std::span<const unsigned char> data, std::size_t& index)
{
    while(index < data.size()) {
        const auto block_size = static_cast<std::size_t>(data[index]);
        ++index;
        if(block_size == 0) {
            return true;
        }
        if(block_size > data.size() - index) {
            return false;
        }
        index += block_size;
    }
    return false;
}

[[nodiscard]] bool validate_gif_structure(std::span<const unsigned char> data)
{
    constexpr std::array<unsigned char, 6> gif87{'G', 'I', 'F', '8', '7', 'a'};
    constexpr std::array<unsigned char, 6> gif89{'G', 'I', 'F', '8', '9', 'a'};
    if(data.size() < 14 || (!has_prefix(data, gif87) && !has_prefix(data, gif89))) {
        return false;
    }

    std::size_t index = 13;
    if(!skip_gif_color_table(data, index, data[10])) {
        return false;
    }

    bool saw_image = false;
    while(index < data.size()) {
        const auto block_type = data[index];
        ++index;
        if(block_type == 0x3b) {
            return saw_image && index == data.size();
        }
        if(block_type == 0x21) {
            if(index >= data.size()) {
                return false;
            }
            ++index;
            if(!skip_gif_sub_blocks(data, index)) {
                return false;
            }
            continue;
        }
        if(block_type == 0x2c) {
            if(data.size() - index < 9) {
                return false;
            }
            const auto width = read_le16(data, index + 4);
            const auto height = read_le16(data, index + 6);
            const auto packed = data[index + 8];
            index += 9;
            if(width == 0 || height == 0 || !skip_gif_color_table(data, index, packed)) {
                return false;
            }
            if(index >= data.size()) {
                return false;
            }
            ++index;
            if(!skip_gif_sub_blocks(data, index)) {
                return false;
            }
            saw_image = true;
            continue;
        }
        if(block_type == 0x00) {
            break;
        }
        return false;
    }
    return false;
}

}

std::optional<ImageInfo> probe_image(std::span<const unsigned char> data)
{
    if(const auto png = probe_png(data)) {
        return png;
    }
    if(const auto jpeg = probe_jpeg(data)) {
        return jpeg;
    }
    if(const auto gif = probe_gif(data)) {
        return gif;
    }
    return probe_webp(data);
}

bool validate_image_decode(std::span<const unsigned char> data, ImageFormat format)
{
    switch(format) {
    case ImageFormat::png:
        return validate_png_decode(data);
    case ImageFormat::jpeg:
        return validate_jpeg_decode(data);
    case ImageFormat::webp:
        return validate_webp_decode(data);
    case ImageFormat::gif:
        return validate_gif_structure(data);
    }
    return false;
}

std::string_view mime_for(ImageFormat format)
{
    switch(format) {
    case ImageFormat::png:
        return "image/png";
    case ImageFormat::jpeg:
        return "image/jpeg";
    case ImageFormat::gif:
        return "image/gif";
    case ImageFormat::webp:
        return "image/webp";
    }
    return "application/octet-stream";
}

std::string_view extension_for(ImageFormat format)
{
    switch(format) {
    case ImageFormat::png:
        return "png";
    case ImageFormat::jpeg:
        return "jpg";
    case ImageFormat::gif:
        return "gif";
    case ImageFormat::webp:
        return "webp";
    }
    return "bin";
}

}
