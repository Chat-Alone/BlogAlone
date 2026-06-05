#pragma once

#include <drogon/HttpFilter.h>

namespace blogalone::filters {

class CsrfFilter : public drogon::HttpFilter<CsrfFilter> {
  public:
    void doFilter(
        const drogon::HttpRequestPtr& request,
        drogon::FilterCallback&& failure,
        drogon::FilterChainCallback&& chain
    ) override;
};

void ensure_csrf_filter_registered();
void install_csrf_guard_advice();

}
