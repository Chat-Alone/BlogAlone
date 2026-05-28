#include "smoke/dependency_checks.h"

#include <sqlite3.h>

namespace blogalone::smoke {

bool sqlite_is_available()
{
    return sqlite3_libversion_number() > 0;
}

}

