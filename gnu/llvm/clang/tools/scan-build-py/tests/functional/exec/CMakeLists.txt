project(exec C)

cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")

include(CheckFunctionExists)
include(CheckSymbolExists)

add_definitions(-D_GNU_SOURCE)
list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)

check_function_exists(execve HAVE_EXECVE)
check_function_exists(execv HAVE_EXECV)
check_function_exists(execvpe HAVE_EXECVPE)
check_function_exists(execvp HAVE_EXECVP)
check_function_exists(execvP HAVE_EXECVP2)
check_function_exists(exect HAVE_EXECT)
check_function_exists(execl HAVE_EXECL)
check_function_exists(execlp HAVE_EXECLP)
check_function_exists(execle HAVE_EXECLE)
check_function_exists(posix_spawn HAVE_POSIX_SPAWN)
check_function_exists(posix_spawnp HAVE_POSIX_SPAWNP)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(exec main.c)
