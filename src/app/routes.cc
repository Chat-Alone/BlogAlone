#include "app/routes.h"

#include "controllers/auth_controller.h"
#include "controllers/forum_controller.h"
#include "controllers/health_controller.h"
#include "controllers/page_controller.h"
#include "filters/csrf_filter.h"
#include "http/api_error.h"
#include "http/request_context.h"

#include <drogon/drogon.h>

#include <utility>

namespace blogalone {

void register_routes()
{
    http::install_request_context_advice();
    filters::install_csrf_guard_advice();
    drogon::app().setCustomErrorHandler(
        [](drogon::HttpStatusCode status, const drogon::HttpRequestPtr& request) {
            const auto code = status == drogon::k404NotFound
                ? http::ErrorCode::not_found
                : http::ErrorCode::internal_error;
            auto error = http::make_api_error(
                code,
                status == drogon::k404NotFound ? "resource not found" : "internal error"
            );
            error.status = status;
            return http::make_error_response(error, http::request_id_from(request));
        }
    );

    controllers::register_health_routes();
    controllers::register_page_routes();
    controllers::register_auth_routes();
    controllers::register_forum_routes();
}

}
