#include "plugins/session_cleanup_plugin.h"

#include "config/app_config.h"
#include "util/time.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <exception>

namespace blogalone::plugins {

void SessionCleanupPlugin::initAndStart(const Json::Value&)
{
    const auto interval = config::app_config_from_drogon().session_cleanup.interval_seconds;
    run_cleanup();
    timer_id_ = drogon::app().getLoop()->runEvery(
        std::chrono::seconds{interval},
        [this]() {
            run_cleanup();
        }
    );
}

void SessionCleanupPlugin::shutdown()
{
    if(timer_id_ != trantor::InvalidTimerId) {
        drogon::app().getLoop()->invalidateTimer(timer_id_);
        timer_id_ = trantor::InvalidTimerId;
    }
}

void SessionCleanupPlugin::run_cleanup() const
{
    try {
        const auto deleted = repository_.delete_expired(util::utc_unix_seconds());
        spdlog::info("session_cleanup sessions_deleted={}", deleted);
    } catch(const std::exception&) {
        spdlog::error("session_cleanup failed=true");
    } catch(...) {
        spdlog::error("session_cleanup failed=true");
    }
}

void ensure_session_cleanup_plugin_registered()
{
    static_cast<void>(SessionCleanupPlugin::classTypeName());
}

}
