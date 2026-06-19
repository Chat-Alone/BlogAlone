#include "db/transaction.h"

#include <stdexcept>

namespace blogalone::db {

Transaction::Transaction(
    const drogon::orm::DbClientPtr& db,
    drogon::orm::TransactionType type
)
    : commit_promise_{std::make_shared<std::promise<bool>>()}
    , commit_result_{commit_promise_->get_future()}
    , transaction_{db->newTransaction(
        [promise = commit_promise_](bool committed) {
            promise->set_value(committed);
        },
        type
    )}
{
}

Transaction::~Transaction()
{
    if(transaction_) {
        transaction_->rollback();
    }
}

drogon::orm::DbClientPtr Transaction::client() const
{
    if(!transaction_) {
        throw std::logic_error{"database transaction is not active"};
    }

    // An owning alias would delay Drogon's implicit commit past commit().
    return {transaction_.get(), [](drogon::orm::DbClient*) {}};
}

void Transaction::commit()
{
    if(!transaction_) {
        throw std::logic_error{"database transaction is not active"};
    }

    transaction_.reset();
    if(!commit_result_.get()) {
        throw std::runtime_error{"database transaction commit failed"};
    }
}

}
