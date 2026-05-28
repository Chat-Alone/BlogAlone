#pragma once

#include "repositories/session_repository.h"
#include "repositories/user_repository.h"

#include <drogon/HttpFilter.h>

namespace blogalone::filters {

class SessionFilter : public drogon::HttpFilter<SessionFilter> {
  public:
    SessionFilter(
        repositories::UserRepository user_repository = repositories::UserRepository{},
        repositories::SessionRepository session_repository = repositories::SessionRepository{}
    );

    void doFilter(
        const drogon::HttpRequestPtr& request,
        drogon::FilterCallback&& failure,
        drogon::FilterChainCallback&& chain
    ) override;

  private:
    repositories::UserRepository user_repository_;
    repositories::SessionRepository session_repository_;
};

class RequireAuthFilter : public drogon::HttpFilter<RequireAuthFilter> {
  public:
    void doFilter(
        const drogon::HttpRequestPtr& request,
        drogon::FilterCallback&& failure,
        drogon::FilterChainCallback&& chain
    ) override;
};

void ensure_session_filters_registered();

}
