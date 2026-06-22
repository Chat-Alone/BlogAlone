#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <trantor/utils/Logger.h>

#include <cstdint>

int main(int argc, char* argv[])
{
    trantor::Logger::setLogLevel(trantor::Logger::kFatal);
    trantor::Logger::setOutputFunction(
        [](const char*, std::uint64_t) {},
        []() {}
    );
    spdlog::set_level(spdlog::level::off);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
