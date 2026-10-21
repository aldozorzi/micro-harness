cmake_minimum_required(VERSION 3.20)
project(micro-harness VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
option(MICRO_HARNESS_NETWORK_TESTS
       "Enable tests that call the live LLM API (requires network access)" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
pkg_check_modules(READLINE REQUIRED readline)

set(SOURCES
    src/main.cpp
    src/Config.cpp
    src/HttpClient.cpp
    src/OpenRouterClient.cpp
    src/SessionManager.cpp
    src/ContextCompactor.cpp
    src/AgentLoop.cpp
)

set(INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CURL_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
    ${READLINE_INCLUDE_DIRS}
)

add_executable(micro-harness ${SOURCES})
target_include_directories(micro-harness PRIVATE ${INCLUDE_DIRS})
target_link_libraries(micro-harness PRIVATE ${CURL_LIBRARIES} ${JSONCPP_LIBRARIES} ${READLINE_LIBRARIES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(micro-harness PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Test suite ─────────────────────────────────────────────────────────────

if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    find_package(GTest QUIET)

    if(GTest_FOUND)
        set(GTEST_TARGETS GTest::gtest GTest::gtest_main)
    else()
        pkg_check_modules(GTEST REQUIRED gtest)
        pkg_check_modules(GTEST_MAIN REQUIRED gtest_main)
        set(GTEST_TARGETS ${GTEST_LIBRARIES} ${GTEST_MAIN_LIBRARIES})
    endif()

    add_executable(test_cli tests/test_cli.cpp)
    target_include_directories(test_cli PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${GTEST_INCLUDE_DIRS})
    target_link_libraries(test_cli PRIVATE ${GTEST_TARGETS} Threads::Threads)

    # Pass the path to the main binary as a compile definition
    target_compile_definitions(test_cli PRIVATE
        MICRO_HARNESS_BIN="${CMAKE_BINARY_DIR}/micro-harness")

    if(MICRO_HARNESS_NETWORK_TESTS)
        target_compile_definitions(test_cli PRIVATE MICRO_HARNESS_NETWORK_TESTS)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_cli PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME cli_tests COMMAND test_cli)
endif()

# ── Install ──────────────────────────────────────────────────────────────────

install(TARGETS micro-harness RUNTIME DESTINATION bin)
install(FILES README.md LICENSE DESTINATION share/doc/micro-harness)
