#pragma once

#include <drogon/orm/DbClient.h>

#include <future>
#include <memory>

namespace blogalone::db {

class Transaction {
  public:
    explicit Transaction(
        const drogon::orm::DbClientPtr& db,
        drogon::orm::TransactionType type
    );

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    ~Transaction();

    [[nodiscard]] drogon::orm::DbClientPtr client() const;
    void commit();

  private:
    std::shared_ptr<std::promise<bool>> commit_promise_;
    std::future<bool> commit_result_;
    std::shared_ptr<drogon::orm::Transaction> transaction_;
};

}
