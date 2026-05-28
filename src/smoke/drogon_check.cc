#include "smoke/dependency_checks.h"

#include <drogon/drogon.h>

namespace blogalone::smoke {

bool drogon_is_available()
{
    const auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k200OK);
    return response->statusCode() == drogon::k200OK;
}

}

