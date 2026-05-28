#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <chrono>
#include <string>
#include <string_view>

namespace blogalone::http {

inline constexpr std::string_view request_id_attribute{"blogalone.request_id"};
inline constexpr std::string_view request_started_at_attribute{"blogalone.request_started_at"};

[[nodiscard]] bool is_valid_request_id(std::string_view value);
[[nodiscard]] std::string make_request_id();
[[nodiscard]] std::string ensure_request_id(const drogon::HttpRequestPtr& request);
[[nodiscard]] std::string request_id_from(const drogon::HttpRequestPtr& request);
void install_request_context_advice();

}
