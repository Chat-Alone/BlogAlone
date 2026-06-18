include_guard(GLOBAL)

find_package(Drogon CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

find_package(unofficial-sqlite3 CONFIG QUIET)
if(NOT unofficial-sqlite3_FOUND)
    find_package(SQLite3 REQUIRED)
endif()

find_package(unofficial-sodium CONFIG QUIET)
if(NOT unofficial-sodium_FOUND)
    find_package(Sodium CONFIG QUIET)
endif()
if(NOT unofficial-sodium_FOUND AND NOT Sodium_FOUND)
    find_package(sodium CONFIG QUIET)
endif()

find_package(cmark-gfm CONFIG REQUIRED)

find_package(SPNG CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(WebP CONFIG REQUIRED)

function(blogalone_select_target output_name)
    foreach(candidate IN LISTS ARGN)
        if(TARGET "${candidate}")
            set("${output_name}" "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    string(REPLACE ";" ", " candidate_list "${ARGN}")
    message(FATAL_ERROR "None of the expected dependency targets exist: ${candidate_list}")
endfunction()

blogalone_select_target(BLOGALONE_DROGON_TARGET
    Drogon::Drogon
    drogon
)

blogalone_select_target(BLOGALONE_SQLITE_TARGET
    unofficial::sqlite3::sqlite3
    SQLite::SQLite3
)

blogalone_select_target(BLOGALONE_SODIUM_TARGET
    unofficial-sodium::sodium
    Sodium::Sodium
    sodium::sodium
    sodium
)

blogalone_select_target(BLOGALONE_CMARK_GFM_TARGET
    cmark-gfm::cmark-gfm
    cmark-gfm
    libcmark-gfm
)

blogalone_select_target(BLOGALONE_SPDLOG_TARGET
    spdlog::spdlog
    spdlog::spdlog_header_only
)

blogalone_select_target(BLOGALONE_SPNG_TARGET
    spng::spng
)

blogalone_select_target(BLOGALONE_JPEG_TARGET
    libjpeg-turbo::jpeg
    JPEG::JPEG
)

blogalone_select_target(BLOGALONE_WEBP_TARGET
    WebP::webp
    WebP::webpdecoder
)

add_library(blogalone_backend_deps INTERFACE)
target_link_libraries(blogalone_backend_deps
    INTERFACE
        ${BLOGALONE_DROGON_TARGET}
        ${BLOGALONE_SQLITE_TARGET}
        ${BLOGALONE_SODIUM_TARGET}
        ${BLOGALONE_CMARK_GFM_TARGET}
        ${BLOGALONE_SPDLOG_TARGET}
        ${BLOGALONE_SPNG_TARGET}
        ${BLOGALONE_JPEG_TARGET}
        ${BLOGALONE_WEBP_TARGET}
)
