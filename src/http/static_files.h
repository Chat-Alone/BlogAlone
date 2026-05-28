#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace blogalone::http {

[[nodiscard]] bool is_safe_relative_resource_path(std::string_view relative_path);
[[nodiscard]] std::optional<std::filesystem::path> resolve_existing_resource(
    const std::filesystem::path& root,
    std::string_view relative_path
);
[[nodiscard]] std::string mime_type_for_static_file(const std::filesystem::path& path);
[[nodiscard]] std::string mime_type_for_upload_file(const std::filesystem::path& path);
[[nodiscard]] drogon::HttpResponsePtr file_response(
    const std::filesystem::path& path,
    std::string_view mime_type,
    const drogon::HttpRequestPtr& request
);

}
