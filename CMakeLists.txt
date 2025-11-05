cmake_minimum_required(VERSION 3.22)
project(kessel C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Debug flags (CLion can switch configs)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g -Wall -Wextra -Wpedantic -Werror)
else()
    add_compile_options(-O2 -Wall -Wextra -Wpedantic -Werror)
endif()

include_directories(include)

set(SRC
        src/main_server.c
        src/ks_config.c
        src/ks_log.c
        src/ks_protocol.c
        src/ks_net.c
        src/ks_server.c
)

add_executable(kessel ${SRC})
