#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4324 4611)
#endif

#include "util/image.h"

#include <spng.h>
#include <turbojpeg.h>
#include <webp/decode.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace blogalone::util {
namespace {

constexpr std::size_t kMaxDecodedImageBytes = 128 * 1024 * 1024;

struct SpngContextDeleter {
    void operator()(spng_ctx* ctx) const { spng_ctx_free(ctx); }
};

struct TurboJpegDeleter {
    void operator()(void* handle) const
    {
        if(handle != nullptr) {
            tjDestroy(handle);
        }
    }
};

using SpngContextPtr = std::unique_ptr<spng_ctx, SpngContextDeleter>;
using TurboJpegPtr = std::unique_ptr<void, TurboJpegDeleter>;

[[nodiscard]] std::optional<std::size_t> decoded_rgba_size(std::uint32_t width, std::uint32_t height)
{
    if(width == 0 || height == 0) {
        return std::nullopt;
    }
    const auto max = (std::numeric_limits<std::size_t>::max)();
    if(static_cast<std::size_t>(width) > max / static_cast<std::size_t>(height)) {
        return std::nullopt;
    }
    const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if(pixels > kMaxDecodedImageBytes / 4) {
        return std::nullopt;
    }
    return pixels * 4;
}

[[nodiscard]] std::optional<std::size_t> decoded_rgba_size(int width, int height)
{
    if(width <= 0 || height <= 0) {
        return std::nullopt;
    }
    return decoded_rgba_size(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
}

[[nodiscard]] std::optional<std::size_t> decoded_pixel_count(std::uint32_t width, std::uint32_t height)
{
    const auto rgba_size = decoded_rgba_size(width, height);
    if(!rgba_size.has_value()) {
        return std::nullopt;
    }
    return *rgba_size / 4;
}

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

[[nodiscard]] bool validate_jpeg_decode(std::span<const unsigned char> data)
{
    if(data.size() > (std::numeric_limits<unsigned long>::max)()) {
        return false;
    }

    TurboJpegPtr handle{tjInitDecompress()};
    if(!handle) {
        return false;
    }

    int width = 0;
    int height = 0;
    int subsampling = TJSAMP_UNKNOWN;
    int colorspace = 0;
    const auto jpeg_size = static_cast<unsigned long>(data.size());
    if(tjDecompressHeader3(
           handle.get(),
           data.data(),
           jpeg_size,
           &width,
           &height,
           &subsampling,
           &colorspace
       ) != 0) {
        return false;
    }

    const auto image_size = decoded_rgba_size(width, height);
    if(!image_size.has_value()) {
        return false;
    }
    std::vector<unsigned char> output(*image_size);
    return tjDecompress2(
        handle.get(),
        data.data(),
        jpeg_size,
        output.data(),
        width,
        0,
        height,
        TJPF_RGBA,
        TJFLAG_STOPONWARNING
    ) == 0;
}

[[nodiscard]] bool validate_webp_decode(std::span<const unsigned char> data)
{
    int width = 0;
    int height = 0;
    if(WebPGetInfo(data.data(), data.size(), &width, &height) == 0) {
        return false;
    }

    const auto image_size = decoded_rgba_size(width, height);
    if(!image_size.has_value()) {
        return false;
    }
    std::vector<unsigned char> output(*image_size);
    return WebPDecodeRGBAInto(
        data.data(),
        data.size(),
        output.data(),
        output.size(),
        width * 4
    ) != nullptr;
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

[[nodiscard]] std::optional<std::vector<unsigned char>> read_gif_sub_blocks(
    std::span<const unsigned char> data,
    std::size_t& index
)
{
    std::vector<unsigned char> output;
    while(index < data.size()) {
        const auto block_size = static_cast<std::size_t>(data[index]);
        ++index;
        if(block_size == 0) {
            return output;
        }
        if(block_size > data.size() - index) {
            return std::nullopt;
        }
        const auto block = data.subspan(index, block_size);
        output.insert(output.end(), block.begin(), block.end());
        index += block_size;
    }
    return std::nullopt;
}

class GifBitReader {
  public:
    explicit GifBitReader(std::span<const unsigned char> data) : data_{data} {}

    [[nodiscard]] std::optional<std::uint16_t> read(std::uint8_t bit_count)
    {
        if(bit_count == 0 || bit_count > 12) {
            return std::nullopt;
        }
        std::uint16_t value = 0;
        for(std::uint8_t bit = 0; bit < bit_count; ++bit) {
            if(bit_index_ / 8 >= data_.size()) {
                return std::nullopt;
            }
            const auto byte = data_[bit_index_ / 8];
            value |= static_cast<std::uint16_t>(((byte >> (bit_index_ % 8)) & 1) << bit);
            ++bit_index_;
        }
        return value;
    }

  private:
    std::span<const unsigned char> data_;
    std::size_t bit_index_{};
};

[[nodiscard]] bool validate_gif_lzw_data(
    std::span<const unsigned char> data,
    unsigned char min_code_size,
    std::size_t expected_pixels
)
{
    if(min_code_size < 2 || min_code_size > 8 || expected_pixels == 0) {
        return false;
    }

    std::array<std::uint32_t, 4096> code_lengths{};
    const auto clear_code = static_cast<std::uint16_t>(1u << min_code_size);
    const auto end_code = static_cast<std::uint16_t>(clear_code + 1);
    auto next_code = static_cast<std::uint16_t>(end_code + 1);
    auto code_size = static_cast<std::uint8_t>(min_code_size + 1);
    std::optional<std::uint32_t> previous_length;

    const auto reset_table = [&]() {
        code_lengths.fill(0);
        for(std::uint16_t code = 0; code < clear_code; ++code) {
            code_lengths[code] = 1;
        }
        next_code = static_cast<std::uint16_t>(end_code + 1);
        code_size = static_cast<std::uint8_t>(min_code_size + 1);
        previous_length = std::nullopt;
    };

    reset_table();
    GifBitReader reader{data};
    bool saw_clear = false;
    std::size_t decoded_pixels = 0;

    while(true) {
        const auto code = reader.read(code_size);
        if(!code.has_value()) {
            return false;
        }
        if(*code == clear_code) {
            reset_table();
            saw_clear = true;
            continue;
        }
        if(!saw_clear) {
            return false;
        }
        if(*code == end_code) {
            return decoded_pixels == expected_pixels;
        }

        std::uint32_t entry_length = 0;
        if(*code < clear_code) {
            entry_length = 1;
        } else if(*code < next_code && code_lengths[*code] != 0) {
            entry_length = code_lengths[*code];
        } else if(previous_length.has_value() && *code == next_code) {
            entry_length = *previous_length + 1;
        } else {
            return false;
        }

        if(entry_length > expected_pixels - decoded_pixels) {
            return false;
        }
        decoded_pixels += entry_length;

        if(previous_length.has_value() && next_code < code_lengths.size()) {
            code_lengths[next_code] = *previous_length + 1;
            ++next_code;
            if(next_code == (1u << code_size) && code_size < 12) {
                ++code_size;
            }
        }
        previous_length = entry_length;
    }
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
            const auto pixel_count = decoded_pixel_count(width, height);
            const auto packed = data[index + 8];
            index += 9;
            if(!pixel_count.has_value() || !skip_gif_color_table(data, index, packed)) {
                return false;
            }
            if(index >= data.size()) {
                return false;
            }
            const auto min_code_size = data[index];
            ++index;
            const auto image_data = read_gif_sub_blocks(data, index);
            if(!image_data.has_value()
                || !validate_gif_lzw_data(*image_data, min_code_size, *pixel_count)) {
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
