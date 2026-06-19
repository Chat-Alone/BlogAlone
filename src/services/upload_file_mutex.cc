#include "services/upload_file_mutex.h"

namespace blogalone::services {

std::mutex& upload_file_mutex()
{
    static std::mutex mutex;
    return mutex;
}

}
