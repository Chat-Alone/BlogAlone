#pragma once

namespace blogalone::smoke {

[[nodiscard]] bool cmark_gfm_is_available();
[[nodiscard]] bool drogon_is_available();
[[nodiscard]] bool sodium_is_available();
[[nodiscard]] bool spdlog_is_available();
[[nodiscard]] bool sqlite_is_available();

}

