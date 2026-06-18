#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace blogalone::models {

struct Upload {
    std::int64_t id{};
    std::string sha256;
    std::string path;
    std::string mime;
    std::int64_t size{};
    std::optional<std::int64_t> width;
    std::optional<std::int64_t> height;
    std::int64_t created_at{};
};

}
