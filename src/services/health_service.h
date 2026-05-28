#pragma once

#include "repositories/health_repository.h"

#include <cstdint>
#include <string>

namespace blogalone::services {

struct HealthStatus {
    std::string status;
    bool database_ok{};
    std::int64_t checked_at{};
};

class HealthService {
  public:
    explicit HealthService(repositories::HealthRepository repository = repositories::HealthRepository{});

    [[nodiscard]] HealthStatus check() const;

  private:
    repositories::HealthRepository repository_;
};

}
