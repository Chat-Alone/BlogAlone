#include "http/request_context.h"

#include "http/client_ip.h"
#include "util/crypto.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <string_view>

namespace blogalone::http {
namespace {

[[nodiscard]] bool is_request_id_char(char ch)
{
    const auto alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
    const auto digit = ch >= '0' && ch <= '9';
    return alpha || digit || ch == '-' || ch == '_' || ch == '.';
}

[[nodiscard]] std::chrono::steady_clock::time_point request_started_at(
    const drogon::HttpRequestPtr& request
)
{
    if(!request->attributes()->find(std::string{request_started_at_attribute})) {
        return std::chrono::steady_clock::now();
    }
    return request->attributes()->get<std::chrono::steady_clock::time_point>(
        std::string{request_started_at_attribute}
    );
}

[[nodiscard]] long long elapsed_milliseconds(const drogon::HttpRequestPtr& request)
{
    const auto started_at = request_started_at(request);
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

}

bool is_valid_request_id(std::string_view value)
{
    constexpr std::size_t kMinLength = 8;
    constexpr std::size_t kMaxLength = 128;

    if(value.size() < kMinLength || value.size() > kMaxLength) {
        return false;
    }
    return std::ranges::all_of(value, is_request_id_char);
}

std::string make_request_id()
{
    return "req_" + util::random_token_hex(16);
}

std::string ensure_request_id(const drogon::HttpRequestPtr& request)
{
    const auto header_value = request->getHeader("x-request-id");
    const auto request_id = is_valid_request_id(header_value) ? header_value : make_request_id();

    request->attributes()->insert(std::string{request_id_attribute}, request_id);
    request->attributes()->insert(
        std::string{request_started_at_attribute},
        std::chrono::steady_clock::now()
    );
    return request_id;
}

std::string request_id_from(const drogon::HttpRequestPtr& request)
{
    if(!request || !request->attributes()->find(std::string{request_id_attribute})) {
        return {};
    }
    return request->attributes()->get<std::string>(std::string{request_id_attribute});
}

void install_request_context_advice()
{
    drogon::app().registerPreRoutingAdvice([](const drogon::HttpRequestPtr& request) {
        static_cast<void>(ensure_request_id(request));
    });

    drogon::app().registerPreSendingAdvice(
        [](const drogon::HttpRequestPtr& request, const drogon::HttpResponsePtr& response) {
            const auto request_id = request_id_from(request);
            if(!request_id.empty()) {
                response->addHeader("X-Request-Id", request_id);
            }

            spdlog::info(
                "request_id={} method={} path={} status={} elapsed_ms={} ip={}",
                request_id,
                request->methodString(),
                request->path(),
                static_cast<int>(response->statusCode()),
                elapsed_milliseconds(request),
                client_ip_from(request)
            );
        }
    );
}

}
