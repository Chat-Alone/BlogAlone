#include "services/health_service.h"

#include "util/time.h"

#include <utility>

namespace blogalone::services {

HealthService::HealthService(repositories::HealthRepository repository)
    : repository_{std::move(repository)}
{
}

HealthStatus HealthService::check() const
{
    const auto database_ok = repository_.can_query_database();
    const auto foreign_keys_ok = database_ok && repository_.foreign_keys_enabled();
    return HealthStatus{
        .status = (database_ok && foreign_keys_ok) ? "ok" : "degraded",
        .database_ok = database_ok,
        .foreign_keys_ok = foreign_keys_ok,
        .checked_at = util::utc_unix_seconds()
    };
}

}
