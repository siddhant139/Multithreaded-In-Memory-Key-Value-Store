cmake_minimum_required(VERSION 3.16)
project(kvstore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(kvstore
    src/main.cpp
    src/server.cpp
    src/thread_pool.cpp
    src/parser.cpp
    src/resp.cpp
    src/persistence.cpp
    src/store.cpp
    src/lru_cache.cpp
    src/expiry_manager.cpp
    src/handler.cpp
)

target_link_libraries(kvstore PRIVATE pthread)

# Optional: Google Test
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest
        GIT_TAG        release-1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    file(GLOB TEST_SOURCES tests/*.cpp)
    add_executable(kvstore_tests ${TEST_SOURCES})
    target_link_libraries(kvstore_tests PRIVATE GTest::gtest_main pthread)

    add_test(NAME kvstore_tests COMMAND kvstore_tests)
endif()
