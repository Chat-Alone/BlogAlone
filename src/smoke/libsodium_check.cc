#include "smoke/dependency_checks.h"

#include <sodium.h>

namespace blogalone::smoke {

bool sodium_is_available()
{
    return sodium_init() >= 0;
}

}

