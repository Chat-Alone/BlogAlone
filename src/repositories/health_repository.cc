#include "repositories/health_repository.h"

#include <drogon/drogon.h>

#include <exception>
#include <utility>

namespace blogalone::repositories {

HealthRepository::HealthRepository(std::string db_client_name)
    : db_client_name_{std::move(db_client_name)}
{
}

bool HealthRepository::can_query_database() const
{
    try {
        const auto client = drogon::app().getDbClient(db_client_name_);
        static_cast<void>(client->execSqlSync("SELECT 1"));
        return true;
    } catch(const std::exception&) {
        return false;
    }
}

}
