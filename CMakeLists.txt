cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_blogalone_vcpkg_candidates)

    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND _blogalone_vcpkg_candidates "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    endif()

    if(CMAKE_HOST_WIN32)
        list(APPEND _blogalone_vcpkg_candidates
            "C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
            "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
            "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
        )
    endif()

    foreach(_blogalone_vcpkg_toolchain IN LISTS _blogalone_vcpkg_candidates)
        if(EXISTS "${_blogalone_vcpkg_toolchain}")
            set(CMAKE_TOOLCHAIN_FILE "${_blogalone_vcpkg_toolchain}" CACHE FILEPATH "vcpkg toolchain file")
            message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
            break()
        endif()
    endforeach()
endif()

project(BlogAlone VERSION 0.1.0 LANGUAGES CXX)

include(cmake/deps.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(blogalone_project_options INTERFACE)
target_compile_features(blogalone_project_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(blogalone_project_options INTERFACE
        /W4
        /permissive-
        /utf-8
        /Zc:__cplusplus
    )
else()
    target_compile_options(blogalone_project_options INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_library(blogalone_core STATIC
    src/app/routes.cc
    src/config/app_config.cc
    src/controllers/auth_controller.cc
    src/controllers/forum_controller.cc
    src/controllers/health_controller.cc
    src/controllers/page_controller.cc
    src/db/migrations.cc
    src/filters/csrf_filter.cc
    src/filters/session_filter.cc
    src/http/api_error.cc
    src/http/request_context.cc
    src/http/session_context.cc
    src/http/static_files.cc
    src/models/user.cc
    src/plugins/database_migration_plugin.cc
    src/repositories/forum_repository.cc
    src/repositories/health_repository.cc
    src/repositories/session_repository.cc
    src/repositories/user_repository.cc
    src/services/auth_service.cc
    src/services/forum_service.cc
    src/services/health_service.cc
    src/util/crypto.cc
    src/util/password.cc
    src/util/time.cc
)
target_include_directories(blogalone_core PUBLIC src)
target_link_libraries(blogalone_core
    PUBLIC
        blogalone_project_options
        blogalone_backend_deps
)

add_executable(blogalone
    src/main.cc
)
target_link_libraries(blogalone
    PRIVATE
        blogalone_core
)

add_library(blogalone_dependency_checks STATIC
    src/smoke/cmark_check.cc
    src/smoke/drogon_check.cc
    src/smoke/libsodium_check.cc
    src/smoke/spdlog_check.cc
    src/smoke/sqlite_check.cc
)
target_include_directories(blogalone_dependency_checks PUBLIC src)
target_link_libraries(blogalone_dependency_checks
    PUBLIC
        blogalone_project_options
        blogalone_backend_deps
)

add_executable(deps_smoke
    src/deps_smoke.cc
)
target_link_libraries(deps_smoke
    PRIVATE
        blogalone_dependency_checks
)

include(CTest)
if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    add_executable(blogalone_unit_tests
        tests/unit/auth_test.cc
        tests/unit/deps_smoke_test.cc
        tests/unit/forum_test.cc
        tests/unit/infrastructure_test.cc
        tests/unit/migration_runner_test.cc
    )
    target_link_libraries(blogalone_unit_tests
        PRIVATE
            blogalone_project_options
            blogalone_core
            blogalone_dependency_checks
            GTest::gtest_main
    )
    target_compile_definitions(blogalone_unit_tests
        PRIVATE
            BLOGALONE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )

    include(GoogleTest)
    gtest_discover_tests(blogalone_unit_tests)
endif()
