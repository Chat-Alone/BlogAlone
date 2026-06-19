#include "plugins/upload_cleanup_plugin.h"

#include "config/app_config.h"
#include "util/time.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <exception>

namespace blogalone::plugins {

void UploadCleanupPlugin::initAndStart(const Json::Value&)
{
    const auto app_config = config::app_config_from_drogon();
    service_ = std::make_unique<services::UploadCleanupService>(app_config.uploads_root);
    retention_seconds_ = app_config.upload_cleanup.retention_seconds;

    run_cleanup();
    timer_id_ = drogon::app().getLoop()->runEvery(
        std::chrono::seconds{app_config.upload_cleanup.interval_seconds},
        [this]() {
            run_cleanup();
        }
    );
}

void UploadCleanupPlugin::shutdown()
{
    if(timer_id_ != trantor::InvalidTimerId) {
        drogon::app().getLoop()->invalidateTimer(timer_id_);
        timer_id_ = trantor::InvalidTimerId;
    }
    service_.reset();
}

void UploadCleanupPlugin::run_cleanup() const
{
    try {
        const auto cutoff = util::utc_unix_seconds() - retention_seconds_;
        const auto result = service_->remove_orphans(cutoff);
        spdlog::info(
            "upload_cleanup refs_deleted={} uploads_deleted={} files_deleted={} file_failures={}",
            result.refs_deleted,
            result.uploads_deleted,
            result.files_deleted,
            result.file_failures
        );
    } catch(const std::exception&) {
        spdlog::error("upload_cleanup failed=true");
    } catch(...) {
        spdlog::error("upload_cleanup failed=true");
    }
}

void ensure_upload_cleanup_plugin_registered()
{
    static_cast<void>(UploadCleanupPlugin::classTypeName());
}

}
