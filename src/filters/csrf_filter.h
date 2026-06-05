#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <optional>

namespace blogalone::filters {

[[nodiscard]] std::optional<drogon::HttpResponsePtr> check_write_csrf(
    const drogon::HttpRequestPtr& request
);
void install_csrf_guard_advice();

}
