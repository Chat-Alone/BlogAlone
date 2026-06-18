#include "util/image.h"

#include <spng.h>
#include <jpeglib.h>
#include <webp/decode.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324 4611)
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <setjmp.h>

namespace blogalone::util {
namespace {

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
    spng_ctx* ctx = spng_ctx_new(0);
    if(ctx == nullptr) {
        return false;
    }
    const int set_buf_result = spng_set_png_buffer(ctx, data.data(), data.size());
    if(set_buf_result != SPNG_OK) {
        spng_ctx_free(ctx);
        return false;
    }
    spng_ihdr ihdr{};
    const int get_ihdr_result = spng_get_ihdr(ctx, &ihdr);
    if(get_ihdr_result != SPNG_OK) {
        spng_ctx_free(ctx);
        return false;
    }
    size_t image_size = 0;
    const int decode_result = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &image_size);
    if(decode_result != SPNG_OK) {
        spng_ctx_free(ctx);
        return false;
    }
    spng_ctx_free(ctx);
    return true;
}

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
    JpegErrorManager error_mgr;
    jpeg_decompress_struct cinfo;
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
    jpeg_destroy_decompress(&cinfo);
    return true;
}

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

[[nodiscard]] bool validate_gif_structure(std::span<const unsigned char> data)
{
    if(data.size() < 10) {
        return false;
    }
    constexpr std::array<unsigned char, 2> trailer{0x3b};
    for(std::size_t i = data.size(); i > 0; --i) {
        if(data[i - 1] == 0x3b) {
            return true;
        }
        if(data[i - 1] != 0x00) {
            break;
        }
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

#ifdef _MSC_VER
#pragma warning(pop)
#endif
