cmake_minimum_required(VERSION 3.22)
project(kessel LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(KESSEL_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(kessel_options INTERFACE)
target_compile_features(kessel_options INTERFACE c_std_17)

if(MSVC)
    target_compile_options(kessel_options INTERFACE /W4 /WX)
else()
    target_compile_options(kessel_options INTERFACE -Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(kessel_options INTERFACE -O2)
    endif()
endif()

if(KESSEL_ENABLE_SANITIZERS)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" AND NOT WIN32)
        target_compile_options(kessel_options INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(kessel_options INTERFACE -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "KESSEL_ENABLE_SANITIZERS requires GCC or Clang on a POSIX platform")
    endif()
endif()

add_library(kessel_core
    src/ks_config.c
    src/ks_ds.c
    src/ks_kv.c
    src/ks_log.c
    src/ks_net.c
    src/ks_protocol.c
    src/ks_pubsub.c
    src/ks_server.c
    src/ks_ttl.c
)
target_include_directories(kessel_core PUBLIC include)
target_link_libraries(kessel_core PUBLIC kessel_options)
if(NOT WIN32)
    target_compile_definitions(kessel_core PRIVATE _POSIX_C_SOURCE=200809L)
endif()

add_executable(kessel src/main_server.c)
target_link_libraries(kessel PRIVATE kessel_core)

add_executable(kessel-cli cli/kessel_cli.c)
target_link_libraries(kessel-cli PRIVATE kessel_options)

find_path(KESSEL_LIBEDIT_INCLUDE_DIR histedit.h)
find_library(KESSEL_LIBEDIT_LIBRARY edit)
if(KESSEL_LIBEDIT_INCLUDE_DIR AND KESSEL_LIBEDIT_LIBRARY)
    target_include_directories(kessel-cli PRIVATE ${KESSEL_LIBEDIT_INCLUDE_DIR})
    target_link_libraries(kessel-cli PRIVATE ${KESSEL_LIBEDIT_LIBRARY})
    target_compile_definitions(kessel-cli PRIVATE KESSEL_HAVE_LIBEDIT=1)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_protocol tests/test_protocol.c)
    target_link_libraries(test_protocol PRIVATE kessel_core)
    add_test(NAME protocol COMMAND test_protocol)

    add_executable(test_config tests/test_config.c)
    target_link_libraries(test_config PRIVATE kessel_core)
    add_test(NAME config COMMAND test_config)

    add_executable(test_kv tests/test_kv.c)
    target_link_libraries(test_kv PRIVATE kessel_core)
    add_test(NAME kv COMMAND test_kv)

    add_executable(test_pubsub tests/test_pubsub.c)
    target_link_libraries(test_pubsub PRIVATE kessel_core)
    add_test(NAME pubsub COMMAND test_pubsub)

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME cli
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py $<TARGET_FILE:kessel-cli> $<TARGET_FILE:kessel>
    )
    set_tests_properties(cli PROPERTIES TIMEOUT 15)

    add_test(
        NAME server_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_server.py $<TARGET_FILE:kessel>
    )
    set_tests_properties(server_integration PROPERTIES TIMEOUT 15)

    add_test(
        NAME hardening
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hardening.py $<TARGET_FILE:kessel>
    )
    set_tests_properties(hardening PROPERTIES TIMEOUT 30)
endif()

add_executable(bench_kv benches/bench_kv.c)
target_link_libraries(bench_kv PRIVATE kessel_core)

add_executable(bench_pubsub benches/bench_pubsub.c)
target_link_libraries(bench_pubsub PRIVATE kessel_core)
