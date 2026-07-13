#pragma once

#include <functional>

namespace blogalone::security {

[[nodiscard]] bool submit_password_task(std::function<void()> task);

}
