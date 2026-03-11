cmake_minimum_required(VERSION 3.22)
project(FullTextSearchService LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14")
        message(FATAL_ERROR "Требуется GCC 14+")
    endif ()
endif ()

include(FetchContent)
set(FETCHCONTENT_BASE_DIR ${CMAKE_SOURCE_DIR}/third_party)

FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        )
FetchContent_MakeAvailable(nlohmann_json)

set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.14.3
        )
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG yaml-cpp-0.7.0
        )
FetchContent_MakeAvailable(yaml-cpp)

add_library(fulltext_search_service_lib
        src/config.cpp
        src/rate_limiter.cpp
        src/tokenizer.cpp
        src/inverted_index.cpp
        src/index_registry.cpp
        src/search.cpp
        src/api_handlers.cpp
        src/api_server.cpp
        )
target_include_directories(fulltext_search_service_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fulltext_search_service_lib PUBLIC
        nlohmann_json::nlohmann_json
        httplib::httplib
        yaml-cpp::yaml-cpp
        )

add_executable(fulltext-search-service src/main.cpp)
target_link_libraries(fulltext-search-service PRIVATE fulltext_search_service_lib)

# Пример клиента
add_executable(example example.cpp)
target_link_libraries(example PRIVATE nlohmann_json::nlohmann_json httplib::httplib)

# Бенчмарк
add_executable(benchmark_load benchmark_load.cpp)
target_link_libraries(benchmark_load PRIVATE nlohmann_json::nlohmann_json httplib::httplib)
