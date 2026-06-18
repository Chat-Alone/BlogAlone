#include "db/migrations.h"
#include "repositories/upload_repository.h"
#include "repositories/user_repository.h"
#include "services/upload_service.h"
#include "util/image.h"

#include <spng.h>
#include <webp/decode.h>

#include <drogon/drogon.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using blogalone::util::ImageFormat;
using blogalone::util::probe_image;
using blogalone::util::validate_image_decode;

[[nodiscard]] std::span<const unsigned char> as_bytes(const std::vector<unsigned char>& data)
{
    return std::span<const unsigned char>{data.data(), data.size()};
}

[[nodiscard]] std::vector<unsigned char> make_png_header_only(unsigned char width, unsigned char height)
{
    return std::vector<unsigned char>{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 'I', 'H', 'D', 'R',
        0x00, 0x00, 0x00, width,
        0x00, 0x00, 0x00, height
    };
}

[[nodiscard]] std::vector<unsigned char> make_valid_1x1_png()
{
    return std::vector<unsigned char>{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
        0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
        0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41,
        0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x01, 0xe2, 0x21, 0xbc,
        0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
        0x44, 0xae, 0x42, 0x60, 0x82
    };
}

[[nodiscard]] std::vector<unsigned char> make_gif_header_only()
{
    return std::vector<unsigned char>{
        'G', 'I', 'F', '8', '9', 'a',
        0x04, 0x00, 0x05, 0x00, 0x00
    };
}

[[nodiscard]] std::vector<unsigned char> make_valid_1x1_gif()
{
    return std::vector<unsigned char>{
        'G', 'I', 'F', '8', '9', 'a',
        0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
        0xff, 0xff, 0xff,
        0x00, 0x00, 0x00,
        0x21, 0xf9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x2c, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x01, 0x00, 0x00,
        0x02, 0x02, 0x44, 0x01, 0x00,
        0x3b
    };
}

[[nodiscard]] std::vector<unsigned char> make_jpeg_header_only()
{
    return std::vector<unsigned char>{
        0xff, 0xd8, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x06, 0x00, 0x07, 0x00
    };
}

[[nodiscard]] std::vector<unsigned char> make_webp_lossless_header_only()
{
    return std::vector<unsigned char>{
        'R', 'I', 'F', 'F', 0x1a, 0x00, 0x00, 0x00,
        'W', 'E', 'B', 'P', 'V', 'P', '8', 'L',
        0x0e, 0x00, 0x00, 0x00, 0x2f, 0x01, 0x80, 0x00, 0x00
    };
}

TEST(ImageProbeTest, ParsesPngHeader)
{
    const auto data = make_png_header_only(2, 3);
    const auto info = probe_image(as_bytes(data));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->format, ImageFormat::png);
    EXPECT_EQ(info->width, 2u);
    EXPECT_EQ(info->height, 3u);
}

TEST(ImageProbeTest, ParsesGifHeader)
{
    const auto data = make_gif_header_only();
    const auto info = probe_image(as_bytes(data));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->format, ImageFormat::gif);
    EXPECT_EQ(info->width, 4u);
    EXPECT_EQ(info->height, 5u);
}

TEST(ImageProbeTest, ParsesJpegHeader)
{
    const auto data = make_jpeg_header_only();
    const auto info = probe_image(as_bytes(data));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->format, ImageFormat::jpeg);
    EXPECT_EQ(info->width, 7u);
    EXPECT_EQ(info->height, 6u);
}

TEST(ImageProbeTest, ParsesWebpLosslessHeader)
{
    const auto data = make_webp_lossless_header_only();
    const auto info = probe_image(as_bytes(data));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->format, ImageFormat::webp);
    EXPECT_EQ(info->width, 2u);
    EXPECT_EQ(info->height, 3u);
}

TEST(ImageProbeTest, RejectsNonImage)
{
    const std::vector<unsigned char> data{'n', 'o', 't', ' ', 'i', 'm', 'g'};
    EXPECT_FALSE(probe_image(as_bytes(data)).has_value());
}

TEST(ImageProbeTest, RejectsTruncatedPng)
{
    std::vector<unsigned char> data{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    EXPECT_FALSE(probe_image(as_bytes(data)).has_value());
}

TEST(ImageDecodeTest, AcceptsValidPng)
{
    const auto data = make_valid_1x1_png();
    EXPECT_TRUE(validate_image_decode(as_bytes(data), ImageFormat::png));
}

TEST(ImageDecodeTest, RejectsPngHeaderOnly)
{
    const auto data = make_png_header_only(2, 3);
    EXPECT_FALSE(validate_image_decode(as_bytes(data), ImageFormat::png));
}

TEST(ImageDecodeTest, AcceptsValidGif)
{
    const auto data = make_valid_1x1_gif();
    EXPECT_TRUE(validate_image_decode(as_bytes(data), ImageFormat::gif));
}

TEST(ImageDecodeTest, RejectsGifHeaderOnly)
{
    const auto data = make_gif_header_only();
    EXPECT_FALSE(validate_image_decode(as_bytes(data), ImageFormat::gif));
}

TEST(ImageDecodeTest, RejectsJpegHeaderOnly)
{
    const auto data = make_jpeg_header_only();
    EXPECT_FALSE(validate_image_decode(as_bytes(data), ImageFormat::jpeg));
}

[[nodiscard]] std::string bytes_to_string(const std::vector<unsigned char>& data)
{
    return std::string{data.begin(), data.end()};
}

class TempWorkspace {
  public:
    TempWorkspace()
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        for(int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = base
                / ("blogalone-upload-test-" + std::to_string(ticks) + "-" + std::to_string(attempt));
            if(std::filesystem::create_directory(candidate)) {
                path_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error{"unable to create temporary test directory"};
    }

    TempWorkspace(const TempWorkspace&) = delete;
    TempWorkspace& operator=(const TempWorkspace&) = delete;

    ~TempWorkspace()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

[[nodiscard]] drogon::orm::DbClientPtr fresh_upload_test_client(
    const std::filesystem::path& database_path
)
{
    static_cast<void>(blogalone::db::run_migrations({
        .database_path = database_path,
        .migrations_dir = std::filesystem::path{BLOGALONE_SOURCE_DIR} / "migrations"
    }));
    const auto client = drogon::orm::DbClient::newSqlite3Client(
        "filename=" + database_path.generic_string(),
        1
    );
    client->execSqlSync("PRAGMA foreign_keys = ON;");
    client->execSqlSync("PRAGMA busy_timeout = 5000;");
    return client;
}

class UploadServiceTest : public testing::Test {
  protected:
    UploadServiceTest()
        : client_{fresh_upload_test_client(workspace_.path() / "blogalone.db")}
        , upload_repository_{client_}
        , service_{
            workspace_.path() / "uploads",
            blogalone::services::UploadLimits{},
            blogalone::repositories::UploadRepository{client_},
            blogalone::repositories::UserRepository{client_}
        }
    {
    }

    [[nodiscard]] std::int64_t insert_user(
        std::string username,
        std::optional<std::int64_t> banned_until = std::nullopt
    )
    {
        if(banned_until.has_value()) {
            const auto rows = client_->execSqlSync(
                "INSERT INTO users (username, email, pwd_hash, banned_until, created_at, updated_at) "
                "VALUES (?, NULL, 'hash', ?, 1, 1) RETURNING id",
                std::move(username),
                *banned_until
            );
            return rows.at(0)["id"].as<std::int64_t>();
        }
        const auto rows = client_->execSqlSync(
            "INSERT INTO users (username, email, pwd_hash, created_at, updated_at) "
            "VALUES (?, NULL, 'hash', 1, 1) RETURNING id",
            std::move(username)
        );
        return rows.at(0)["id"].as<std::int64_t>();
    }

    TempWorkspace workspace_;
    drogon::orm::DbClientPtr client_;
    blogalone::repositories::UploadRepository upload_repository_;
    blogalone::services::UploadService service_;
};

TEST_F(UploadServiceTest, StoresValidPng)
{
    const auto owner = insert_user("alice");
    const auto content = bytes_to_string(make_valid_1x1_png());
    const auto result = service_.store_image(owner, content, 1'700'000'000);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->mime, "image/png");
    EXPECT_EQ(result->width, 1);
    EXPECT_EQ(result->height, 1);
    EXPECT_TRUE(result->url.starts_with("/uploads/"));

    const auto relative = result->url.substr(std::string_view{"/uploads/"}.size());
    EXPECT_TRUE(std::filesystem::exists(workspace_.path() / "uploads" / relative));
    EXPECT_TRUE(upload_repository_.owner_has_upload_path(owner, relative));
}

TEST_F(UploadServiceTest, RejectsPngHeaderOnly)
{
    const auto owner = insert_user("alice");
    const auto content = bytes_to_string(make_png_header_only(2, 3));
    const auto result = service_.store_image(owner, content, 1'700'000'000);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), blogalone::services::UploadError::unsupported_type);
}

TEST_F(UploadServiceTest, DeduplicatesIdenticalContent)
{
    const auto owner = insert_user("alice");
    const auto content = bytes_to_string(make_valid_1x1_png());
    const auto first = service_.store_image(owner, content, 1'700'000'000);
    const auto second = service_.store_image(owner, content, 1'700'000'000);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->url, second->url);

    const auto rows = client_->execSqlSync("SELECT COUNT(*) AS total FROM uploads");
    EXPECT_EQ(rows.at(0)["total"].as<std::int64_t>(), 1);
}

TEST_F(UploadServiceTest, RejectsNonImage)
{
    const auto owner = insert_user("alice");
    const auto result = service_.store_image(owner, "not an image", 1'700'000'000);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), blogalone::services::UploadError::unsupported_type);
}

TEST_F(UploadServiceTest, RejectsBannedUser)
{
    const auto owner = insert_user("bob", 2'000'000'000);
    const auto content = bytes_to_string(make_valid_1x1_png());
    const auto result = service_.store_image(owner, content, 1'700'000'000);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), blogalone::services::UploadError::forbidden);
}

TEST_F(UploadServiceTest, RejectsOversizedFile)
{
    blogalone::services::UploadService limited{
        workspace_.path() / "uploads",
        blogalone::services::UploadLimits{.max_file_size = 4},
        blogalone::repositories::UploadRepository{client_},
        blogalone::repositories::UserRepository{client_}
    };
    const auto owner = insert_user("alice");
    const auto content = bytes_to_string(make_valid_1x1_png());
    const auto result = limited.store_image(owner, content, 1'700'000'000);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), blogalone::services::UploadError::too_large);
}

TEST_F(UploadServiceTest, EnforcesDailyQuota)
{
    blogalone::services::UploadService limited{
        workspace_.path() / "uploads",
        blogalone::services::UploadLimits{.max_daily_uploads = 1},
        blogalone::repositories::UploadRepository{client_},
        blogalone::repositories::UserRepository{client_}
    };
    const auto owner = insert_user("alice");
    const auto first = limited.store_image(owner, bytes_to_string(make_valid_1x1_png()), 1'700'000'000);
    const auto second = limited.store_image(owner, bytes_to_string(make_valid_1x1_gif()), 1'700'000'000);

    ASSERT_TRUE(first.has_value());
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), blogalone::services::UploadError::rate_limited);
}

}
