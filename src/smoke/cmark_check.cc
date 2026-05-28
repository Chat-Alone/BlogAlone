#include "smoke/dependency_checks.h"

#include <cmark-gfm.h>

#include <string_view>

namespace blogalone::smoke {

bool cmark_gfm_is_available()
{
    return !std::string_view{cmark_version_string()}.empty();
}

}

