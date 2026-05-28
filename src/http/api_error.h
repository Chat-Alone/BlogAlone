#pragma once

#include <drogon/HttpResponse.h>

#include <string>
#include <string_view>

namespace blogalone::http {

enum class ErrorCode {
    invalid_argument,
    unauthenticated,
    forbidden,
    admin_reauth_required,
    not_found,
    conflict,
    rate_limited,
    internal_error
};

struct ApiError {
    ErrorCode code{ErrorCode::internal_error};
    std::string message{"internal error"};
    drogon::HttpStatusCode status{drogon::k500InternalServerError};
};

[[nodiscard]] std::string_view to_string(ErrorCode code);
[[nodiscard]] drogon::HttpStatusCode default_status(ErrorCode code);
[[nodiscard]] ApiError make_api_error(ErrorCode code, std::string message);
[[nodiscard]] drogon::HttpResponsePtr make_error_response(
    const ApiError& error,
    std::string_view request_id
);

}
