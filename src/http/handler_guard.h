#pragma once

#include "http/api_error.h"
#include "http/request_context.h"

#include <drogon/HttpRequest.h>
#include <drogon/orm/Exception.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <string_view>
#include <utility>

namespace blogalone::http {

template <typename Callback, typename Function>
void run_guarded_request(
    const drogon::HttpRequestPtr& request,
    const Callback& callback,
    std::string_view scope,
    Function&& function
)
{
    try {
        std::forward<Function>(function)();
    } catch(const drogon::orm::DrogonDbException&) {
        spdlog::error(
            "request_id={} scope={} database_error=true",
            request_id_from(request),
            scope
        );
        callback(make_internal_error_response(request));
    } catch(const std::exception&) {
        spdlog::error(
            "request_id={} scope={} exception=true",
            request_id_from(request),
            scope
        );
        callback(make_internal_error_response(request));
    } catch(...) {
        spdlog::error(
            "request_id={} scope={} unknown_exception=true",
            request_id_from(request),
            scope
        );
        callback(make_internal_error_response(request));
    }
}

}
