#include "controllers/health_controller.h"

#include "services/health_service.h"

#include <drogon/drogon.h>
#include <json/value.h>

#include <functional>
#include <utility>

namespace blogalone::controllers {

void register_health_routes()
{
    drogon::app().registerHandler(
        "/api/healthz",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const auto status = services::HealthService{}.check();

            Json::Value body;
            body["status"] = status.status;
            body["checked_at"] = Json::Int64{status.checked_at};
            body["dependencies"]["database"] = status.database_ok;

            auto response = drogon::HttpResponse::newHttpJsonResponse(body);
            if(!status.database_ok) {
                response->setStatusCode(drogon::k503ServiceUnavailable);
            }
            callback(std::move(response));
        },
        {drogon::Get}
    );
}

}
