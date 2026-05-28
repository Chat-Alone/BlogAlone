#include "smoke/dependency_checks.h"

#include <spdlog/spdlog.h>

namespace blogalone::smoke {

bool spdlog_is_available()
{
    spdlog::info("BlogAlone dependency smoke target linked successfully");
    return spdlog::default_logger() != nullptr;
}

}

