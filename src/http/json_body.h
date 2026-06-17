#pragma once

#include <drogon/HttpRequest.h>
#include <json/value.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace blogalone::http {

[[nodiscard]] std::shared_ptr<Json::Value> parse_json_body(const drogon::HttpRequestPtr& request);
[[nodiscard]] std::optional<std::string> json_string(
    const Json::Value& object,
    std::string_view key,
    bool& type_error
);
[[nodiscard]] std::optional<std::int64_t> json_int64(
    const Json::Value& object,
    std::string_view key,
    bool& type_error
);

}
