cmake_minimum_required(VERSION 3.22)
project(kessel C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SRC
        src/main_server.c
        src/ks_net.c
        src/ks_protocol.c
        src/ks_server.c
        src/ks_log.c
        include/hs_log.h
        include/hs_log.h
        src/main_server.c
)

include_directories(include)

add_executable(kessel ${SRC}
        include/hs_log.h)
