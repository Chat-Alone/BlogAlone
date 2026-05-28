#include "filters/csrf_filter.h"

#include "http/api_error.h"
#include "http/request_context.h"
#include "http/session_context.h"
#include "util/crypto.h"

#include <drogon/HttpTypes.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace blogalone::filters {
namespace {

[[nodiscard]] bool is_write_method(drogon::HttpMethod method)
{
    return method == drogon::Post
        || method == drogon::Put
        || method == drogon::Patch
        || method == drogon::Delete;
}

[[nodiscard]] drogon::HttpResponsePtr forbidden(
    const drogon::HttpRequestPtr& request,
    std::string message
)
{
    return http::make_error_response(
        http::make_api_error(http::ErrorCode::forbidden, std::move(message)),
        http::request_id_from(request)
    );
}

[[nodiscard]] std::string_view trim_ascii(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if(first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

[[nodiscard]] std::string to_lower_ascii(std::string_view value)
{
    std::string output(value.size(), '\0');
    std::ranges::transform(value, output.begin(), [](char ch) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    });
    return output;
}

[[nodiscard]] bool contains_invalid_host_char(std::string_view value)
{
    return std::ranges::any_of(value, [](char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == ',';
    });
}

[[nodiscard]] std::optional<std::string> normalized_request_host(
    const drogon::HttpRequestPtr& request
)
{
    const auto host = trim_ascii(request->getHeader("host"));
    if(host.empty() || contains_invalid_host_char(host)) {
        return std::nullopt;
    }
    return to_lower_ascii(host);
}

[[nodiscard]] std::optional<std::string> normalized_url_host(std::string_view value)
{
    const auto trimmed = trim_ascii(value);
    const auto scheme_end = trimmed.find("://");
    if(scheme_end == std::string_view::npos || scheme_end == 0) {
        return std::nullopt;
    }

    const auto host_begin = scheme_end + 3;
    const auto host_end = trimmed.find_first_of("/?#", host_begin);
    auto host = trimmed.substr(host_begin, host_end - host_begin);
    if(host.empty() || host.find('@') != std::string_view::npos || contains_invalid_host_char(host)) {
        return std::nullopt;
    }
    return to_lower_ascii(host);
}

[[nodiscard]] bool same_origin_source(
    const drogon::HttpRequestPtr& request,
    std::string_view source
)
{
    const auto request_host = normalized_request_host(request);
    const auto source_host = normalized_url_host(source);
    return request_host.has_value() && source_host.has_value() && *request_host == *source_host;
}

[[nodiscard]] bool same_origin_write_request(const drogon::HttpRequestPtr& request)
{
    const auto origin = request->getHeader("origin");
    if(!origin.empty() && !same_origin_source(request, origin)) {
        return false;
    }

    const auto referer = request->getHeader("referer");
    if(!referer.empty() && !same_origin_source(request, referer)) {
        return false;
    }

    return true;
}

}

void CsrfFilter::doFilter(
    const drogon::HttpRequestPtr& request,
    drogon::FilterCallback&& failure,
    drogon::FilterChainCallback&& chain
)
{
    if(!is_write_method(request->method())) {
        chain();
        return;
    }

    const auto session = http::session_context_of(request);
    if(!session.has_value()) {
        failure(http::make_error_response(
            http::make_api_error(http::ErrorCode::unauthenticated, "authentication required"),
            http::request_id_from(request)
        ));
        return;
    }

    if(!same_origin_write_request(request)) {
        failure(forbidden(request, "cross-site request rejected"));
        return;
    }

    const auto provided = request->getHeader(std::string{http::csrf_header_name});
    if(provided.empty()) {
        failure(forbidden(request, "missing csrf token"));
        return;
    }
    if(util::sha256_hex(provided) != session->csrf_hash) {
        failure(forbidden(request, "invalid csrf token"));
        return;
    }

    chain();
}

void ensure_csrf_filter_registered()
{
    static_cast<void>(CsrfFilter::classTypeName());
}

}
