#include "security/password_task_queue.h"

#include <trantor/utils/ConcurrentTaskQueue.h>

#include <cstddef>
#include <semaphore>
#include <utility>

namespace blogalone::security {
namespace {

class PasswordTaskQueue {
  public:
    [[nodiscard]] bool submit(std::function<void()> task)
    {
        if(!available_slots_.try_acquire()) {
            return false;
        }

        try {
            queue_.runTaskInQueue([this, task = std::move(task)]() mutable {
                const SlotReleaser slot_releaser{available_slots_};
                task();
            });
        } catch(...) {
            available_slots_.release();
            throw;
        }
        return true;
    }

  private:
    static constexpr std::ptrdiff_t MAX_PENDING_TASKS = 32;
    static constexpr std::size_t WORKER_COUNT = 2;

    class SlotReleaser {
      public:
        explicit SlotReleaser(
            std::counting_semaphore<MAX_PENDING_TASKS>& available_slots
        ) noexcept
            : available_slots_{available_slots}
        {
        }

        SlotReleaser(const SlotReleaser&) = delete;
        SlotReleaser& operator=(const SlotReleaser&) = delete;

        ~SlotReleaser()
        {
            available_slots_.release();
        }

      private:
        std::counting_semaphore<MAX_PENDING_TASKS>& available_slots_;
    };

    std::counting_semaphore<MAX_PENDING_TASKS> available_slots_{MAX_PENDING_TASKS};
    trantor::ConcurrentTaskQueue queue_{WORKER_COUNT, "password"};
};

[[nodiscard]] PasswordTaskQueue& password_task_queue()
{
    static PasswordTaskQueue queue;
    return queue;
}

}

bool submit_password_task(std::function<void()> task)
{
    return password_task_queue().submit(std::move(task));
}

}
