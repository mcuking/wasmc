cmake_minimum_required(VERSION 3.19)
project(wasmc C)

set(CMAKE_C_STANDARD 11)

set(SOURCES_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCES
        ${SOURCES_ROOT}/source/cli.c
        ${SOURCES_ROOT}/source/module.c
        ${SOURCES_ROOT}/source/utils.c
        ${SOURCES_ROOT}/source/interpreter.c)

add_executable(wasmc ${SOURCES})

target_link_libraries(wasmc readline m dl)