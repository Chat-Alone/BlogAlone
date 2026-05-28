#include "smoke/dependency_checks.h"

int main()
{
    if(!blogalone::smoke::sqlite_is_available()) {
        return 1;
    }

    if(!blogalone::smoke::sodium_is_available()) {
        return 2;
    }

    if(!blogalone::smoke::cmark_gfm_is_available()) {
        return 3;
    }

    if(!blogalone::smoke::drogon_is_available()) {
        return 4;
    }

    return blogalone::smoke::spdlog_is_available() ? 0 : 5;
}
