#include "controllers/page_controller.h"

#include "config/app_config.h"
#include "http/api_error.h"
#include "http/request_context.h"
#include "http/static_files.h"

#include <drogon/drogon.h>

#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace blogalone::controllers {
namespace {

[[nodiscard]] drogon::HttpResponsePtr not_found_response(const drogon::HttpRequestPtr& request)
{
    return http::make_error_response(
        http::make_api_error(http::ErrorCode::not_found, "resource not found"),
        http::request_id_from(request)
    );
}

void send_static_file(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::filesystem::path& root,
    const std::string& relative_path
)
{
    const auto file = http::resolve_existing_resource(root, relative_path);
    if(!file) {
        callback(not_found_response(request));
        return;
    }

    callback(http::file_response(*file, http::mime_type_for_static_file(*file), request));
}

void send_upload_file(
    const drogon::HttpRequestPtr& request,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::filesystem::path& root,
    const std::string& relative_path
)
{
    const auto file = http::resolve_existing_resource(root, relative_path);
    if(!file) {
        callback(not_found_response(request));
        return;
    }

    auto response = http::file_response(*file, http::mime_type_for_upload_file(*file), request);
    response->addHeader("X-Content-Type-Options", "nosniff");
    callback(std::move(response));
}

}

void register_page_routes()
{
    drogon::app().registerHandler(
        "/",
        [](const drogon::HttpRequestPtr& request,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto app_config = config::app_config_from_drogon();
            send_static_file(
                request,
                std::move(callback),
                app_config.web_root / "pages",
                "index.html"
            );
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/threads/{1}",
        [](const drogon::HttpRequestPtr& request,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           std::string) {
            const auto app_config = config::app_config_from_drogon();
            send_static_file(
                request,
                std::move(callback),
                app_config.web_root / "pages",
                "thread.html"
            );
        },
        {drogon::Get}
    );

    drogon::app().registerHandlerViaRegex(
        R"(^/static/(.+)$)",
        [](const drogon::HttpRequestPtr& request,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           std::string relative_path) {
            const auto app_config = config::app_config_from_drogon();
            send_static_file(
                request,
                std::move(callback),
                app_config.web_root / "static",
                relative_path
            );
        },
        {drogon::Get}
    );

    drogon::app().registerHandlerViaRegex(
        R"(^/uploads/(.+)$)",
        [](const drogon::HttpRequestPtr& request,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           std::string relative_path) {
            const auto app_config = config::app_config_from_drogon();
            send_upload_file(request, std::move(callback), app_config.uploads_root, relative_path);
        },
        {drogon::Get}
    );
}

}
