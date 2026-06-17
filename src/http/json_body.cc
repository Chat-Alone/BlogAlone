#include "http/json_body.h"

namespace blogalone::http {

std::shared_ptr<Json::Value> parse_json_body(const drogon::HttpRequestPtr& request)
{
    return request->getJsonObject();
}

std::optional<std::string> json_string(
    const Json::Value& object,
    std::string_view key,
    bool& type_error
)
{
    const auto* value = object.find(key.data(), key.data() + key.size());
    if(value == nullptr || value->isNull()) {
        return std::nullopt;
    }
    if(!value->isString()) {
        type_error = true;
        return std::nullopt;
    }
    return value->asString();
}

std::optional<std::int64_t> json_int64(
    const Json::Value& object,
    std::string_view key,
    bool& type_error
)
{
    const auto* value = object.find(key.data(), key.data() + key.size());
    if(value == nullptr || value->isNull()) {
        return std::nullopt;
    }
    if(!value->isInt64() && !value->isUInt64()) {
        type_error = true;
        return std::nullopt;
    }
    return value->asInt64();
}

}
