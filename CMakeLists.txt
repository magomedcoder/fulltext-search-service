cmake_minimum_required(VERSION 3.22)
project(FullTextSearchService LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14")
        message(FATAL_ERROR "Требуется GCC 14+")
    endif ()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "18")
        message(FATAL_ERROR "Требуется Clang 18+")
    endif ()
endif ()

add_library(fulltext_search_service_lib
        src/tokenizer.cpp
        src/inverted_index.cpp
        src/search.cpp
        )
target_include_directories(fulltext_search_service_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_executable(fulltext-search-service src/main.cpp)
target_link_libraries(fulltext-search-service PRIVATE fulltext_search_service_lib)
