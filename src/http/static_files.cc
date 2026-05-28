#include "http/static_files.h"

#include "http/api_error.h"
#include "http/request_context.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace blogalone::http {
namespace {

[[nodiscard]] std::string lower_extension(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

}

bool is_safe_relative_resource_path(std::string_view relative_path)
{
    if(relative_path.empty() || relative_path.starts_with('/') || relative_path.starts_with('\\')) {
        return false;
    }

    std::size_t segment_start = 0;
    while(segment_start <= relative_path.size()) {
        const auto separator = relative_path.find_first_of("/\\", segment_start);
        const auto segment_end = separator == std::string_view::npos
            ? relative_path.size()
            : separator;
        const auto segment = relative_path.substr(segment_start, segment_end - segment_start);
        if(segment.empty() || segment == "." || segment == ".." || segment.find(':') != std::string_view::npos) {
            return false;
        }
        if(separator == std::string_view::npos) {
            break;
        }
        segment_start = separator + 1;
    }

    return true;
}

std::optional<std::filesystem::path> resolve_existing_resource(
    const std::filesystem::path& root,
    std::string_view relative_path
)
{
    if(!is_safe_relative_resource_path(relative_path)) {
        return std::nullopt;
    }

    const auto candidate = (root / std::filesystem::path{std::string{relative_path}}).lexically_normal();
    std::error_code error;
    if(!std::filesystem::is_regular_file(candidate, error)) {
        return std::nullopt;
    }
    return candidate;
}

std::string mime_type_for_static_file(const std::filesystem::path& path)
{
    const auto extension = lower_extension(path);
    if(extension == ".html") {
        return "text/html; charset=utf-8";
    }
    if(extension == ".css") {
        return "text/css; charset=utf-8";
    }
    if(extension == ".js") {
        return "application/javascript; charset=utf-8";
    }
    if(extension == ".json") {
        return "application/json; charset=utf-8";
    }
    if(extension == ".svg") {
        return "image/svg+xml";
    }
    if(extension == ".png") {
        return "image/png";
    }
    if(extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if(extension == ".webp") {
        return "image/webp";
    }
    if(extension == ".gif") {
        return "image/gif";
    }
    return "application/octet-stream";
}

std::string mime_type_for_upload_file(const std::filesystem::path& path)
{
    const auto extension = lower_extension(path);
    if(extension == ".png") {
        return "image/png";
    }
    if(extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if(extension == ".gif") {
        return "image/gif";
    }
    if(extension == ".webp") {
        return "image/webp";
    }
    return "application/octet-stream";
}

drogon::HttpResponsePtr file_response(
    const std::filesystem::path& path,
    std::string_view mime_type,
    const drogon::HttpRequestPtr& request
)
{
    auto response = drogon::HttpResponse::newFileResponse(
        path.string(),
        "",
        drogon::CT_CUSTOM,
        std::string{mime_type},
        request
    );
    return response;
}

}
