#pragma once

#include <string>

namespace blogalone::repositories {

class HealthRepository {
  public:
    explicit HealthRepository(std::string db_client_name = "default");

    [[nodiscard]] bool can_query_database() const;
    [[nodiscard]] bool foreign_keys_enabled() const;

  private:
    std::string db_client_name_;
};

}
