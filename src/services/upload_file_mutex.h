#pragma once

#include <mutex>

namespace blogalone::services {

[[nodiscard]] std::mutex& upload_file_mutex();

}
