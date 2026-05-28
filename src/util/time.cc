#include "util/time.h"

#include <chrono>

namespace blogalone::util {

std::int64_t utc_unix_seconds()
{
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

}
