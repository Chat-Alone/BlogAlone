#pragma once

#include "models/upload.h"

#include <drogon/orm/DbClient.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blogalone::repositories {

class UploadRepository {
  public:
    explicit UploadRepository(std::string db_client_name = "default");
    explicit UploadRepository(drogon::orm::DbClientPtr db_client);

    [[nodiscard]] drogon::orm::DbClientPtr client() const;
    [[nodiscard]] std::optional<models::Upload> find_by_sha256(std::string_view sha256) const;
    [[nodiscard]] std::optional<std::int64_t> create_upload(
        std::string_view sha256,
        std::string_view path,
        std::string_view mime,
        std::int64_t size,
        std::optional<std::int64_t> width,
        std::optional<std::int64_t> height,
        std::int64_t created_at
    ) const;
    [[nodiscard]] bool create_ref(std::int64_t owner_id, std::int64_t upload_id, std::int64_t created_at) const;
    [[nodiscard]] std::int64_t count_owner_refs_since(std::int64_t owner_id, std::int64_t since) const;
    [[nodiscard]] bool owner_has_upload_path(std::int64_t owner_id, std::string_view path) const;
    void mark_ref_attached(std::int64_t owner_id, std::string_view path, std::int64_t attached_at) const;
    [[nodiscard]] std::int64_t delete_unattached_refs_before(std::int64_t cutoff) const;
    [[nodiscard]] std::vector<models::Upload> delete_unreferenced_uploads() const;

  private:
    std::string db_client_name_;
    drogon::orm::DbClientPtr db_client_;
};

}
