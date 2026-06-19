#pragma once

#include "services/upload_cleanup_service.h"

#include <drogon/plugins/Plugin.h>
#include <trantor/net/EventLoop.h>

#include <cstdint>
#include <memory>

namespace blogalone::plugins {

class UploadCleanupPlugin final : public drogon::Plugin<UploadCleanupPlugin> {
  public:
    UploadCleanupPlugin() = default;

    void initAndStart(const Json::Value& config) override;
    void shutdown() override;

  private:
    void run_cleanup() const;

    std::unique_ptr<services::UploadCleanupService> service_;
    std::int64_t retention_seconds_{};
    trantor::TimerId timer_id_{trantor::InvalidTimerId};
};

void ensure_upload_cleanup_plugin_registered();

}
