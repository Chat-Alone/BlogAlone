#include "controllers/upload_controller.h"

#include "config/app_config.h"
#include "filters/session_filter.h"
#include "http/api_error.h"
#include "http/handler_guard.h"
#include "http/request_context.h"
#include "http/session_context.h"
#include "services/upload_service.h"
#include "util/time.h"

#include <drogon/MultiPart.h>
#include <drogon/drogon.h>
#include <json/value.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace blogalone::controllers {
namespace {

using HttpCallback = std::function<void(const drogon::HttpResponsePtr&)>;

constexpr std::string_view kUploadFilePartName{"file"};

[[nodiscard]] http::ErrorCode to_error_code(services::UploadError error)
{
    switch(error) {
    case services::UploadError::invalid_input:
    case services::UploadError::unsupported_type:
    case services::UploadError::too_large:
        return http::ErrorCode::invalid_argument;
    case services::UploadError::rate_limited:
        return http::ErrorCode::rate_limited;
    case services::UploadError::not_found:
        return http::ErrorCode::not_found;
    case services::UploadError::forbidden:
        return http::ErrorCode::forbidden;
    case services::UploadError::internal_error:
        return http::ErrorCode::internal_error;
    }
    return http::ErrorCode::internal_error;
}

[[nodiscard]] drogon::HttpResponsePtr error_response(
    const drogon::HttpRequestPtr& request,
    http::ErrorCode code,
    std::string message
)
{
    return http::make_error_response(
        http::make_api_error(code, std::move(message)),
        http::request_id_from(request)
    );
}

[[nodiscard]] std::optional<std::int64_t> current_user_id(const drogon::HttpRequestPtr& request)
{
    const auto session = http::session_context_of(request);
    if(!session.has_value()) {
        return std::nullopt;
    }
    return session->user_id;
}

void handle_create_upload(const drogon::HttpRequestPtr& request, const HttpCallback& callback)
{
    const auto user_id = current_user_id(request);
    if(!user_id.has_value()) {
        callback(error_response(request, http::ErrorCode::unauthenticated, "authentication required"));
        return;
    }

    drogon::MultiPartParser parser;
    if(parser.parse(request) != 0 || parser.getFiles().size() != 1) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "expected a single file part"));
        return;
    }

    const auto& file = parser.getFiles().front();
    if(std::string_view{file.getItemName()} != kUploadFilePartName) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "expected file part named file"));
        return;
    }

    const auto app_config = config::app_config_from_drogon();
    const services::UploadLimits limits{
        .max_file_size = app_config.upload.max_file_size,
        .max_daily_uploads = app_config.upload.max_daily_uploads,
        .max_dimension = app_config.upload.max_dimension
    };
    const services::UploadService service{app_config.uploads_root, limits};
    const auto result = service.store_image(*user_id, file.fileContent(), util::utc_unix_seconds());
    if(!result.has_value()) {
        callback(error_response(
            request,
            to_error_code(result.error()),
            std::string{services::to_string(result.error())}
        ));
        return;
    }

    Json::Value upload;
    upload["url"] = result->url;
    upload["mime"] = result->mime;
    upload["size"] = static_cast<Json::Int64>(result->size);
    upload["width"] = static_cast<Json::Int64>(result->width);
    upload["height"] = static_cast<Json::Int64>(result->height);

    Json::Value body;
    body["upload"] = upload;
    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(drogon::k201Created);
    callback(response);
}

}

void register_upload_routes()
{
    filters::ensure_session_filters_registered();

    drogon::app().registerHandler(
        "/api/uploads",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "uploads.create", [&]() {
                handle_create_upload(request, callback);
            });
        },
        {drogon::Post,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );
}

}
