#include "http/api_error.h"

#include "http/request_context.h"

#include <json/value.h>

#include <utility>

namespace blogalone::http {

std::string_view to_string(ErrorCode code)
{
    switch(code) {
    case ErrorCode::invalid_argument:
        return "invalid_argument";
    case ErrorCode::unauthenticated:
        return "unauthenticated";
    case ErrorCode::forbidden:
        return "forbidden";
    case ErrorCode::admin_reauth_required:
        return "admin_reauth_required";
    case ErrorCode::not_found:
        return "not_found";
    case ErrorCode::conflict:
        return "conflict";
    case ErrorCode::rate_limited:
        return "rate_limited";
    case ErrorCode::internal_error:
        return "internal_error";
    }

    return "internal_error";
}

drogon::HttpStatusCode default_status(ErrorCode code)
{
    switch(code) {
    case ErrorCode::invalid_argument:
        return drogon::k400BadRequest;
    case ErrorCode::unauthenticated:
        return drogon::k401Unauthorized;
    case ErrorCode::forbidden:
    case ErrorCode::admin_reauth_required:
        return drogon::k403Forbidden;
    case ErrorCode::not_found:
        return drogon::k404NotFound;
    case ErrorCode::conflict:
        return drogon::k409Conflict;
    case ErrorCode::rate_limited:
        return drogon::k429TooManyRequests;
    case ErrorCode::internal_error:
        return drogon::k500InternalServerError;
    }

    return drogon::k500InternalServerError;
}

ApiError make_api_error(ErrorCode code, std::string message)
{
    return ApiError{
        .code = code,
        .message = std::move(message),
        .status = default_status(code)
    };
}

drogon::HttpResponsePtr make_internal_error_response(const drogon::HttpRequestPtr& request)
{
    return make_error_response(
        make_api_error(ErrorCode::internal_error, "internal error"),
        request_id_from(request)
    );
}

drogon::HttpResponsePtr make_error_response(const ApiError& error, std::string_view request_id)
{
    Json::Value body;
    body["error"]["code"] = std::string{to_string(error.code)};
    body["error"]["message"] = error.message;
    body["error"]["request_id"] = std::string{request_id};

    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    response->setStatusCode(error.status);
    if(!request_id.empty()) {
        response->addHeader("X-Request-Id", std::string{request_id});
    }
    return response;
}

}
