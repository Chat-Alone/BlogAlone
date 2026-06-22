#pragma once

#include "repositories/session_repository.h"

#include <drogon/plugins/Plugin.h>
#include <trantor/net/EventLoop.h>

namespace blogalone::plugins {

class SessionCleanupPlugin final : public drogon::Plugin<SessionCleanupPlugin> {
  public:
    SessionCleanupPlugin() = default;

    void initAndStart(const Json::Value& config) override;
    void shutdown() override;

  private:
    void run_cleanup() const;

    repositories::SessionRepository repository_;
    trantor::TimerId timer_id_{trantor::InvalidTimerId};
};

void ensure_session_cleanup_plugin_registered();

}
