cmake_minimum_required(VERSION 3.25)

# ---- Misc: sanitizers variable, -pie, compile_commands.json, ccache ----
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(USE_SANITIZER "address,undefined" CACHE STRING
    "Sanitizer to use: address, thread, undefined, leak, none")
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

project(
    cppserver
    VERSION 0.1.0
    DESCRIPTION "Simple C++ Web Server"
    LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Dependencies ----

find_package(Threads REQUIRED)

# ---- Compile options (interface targets) ----

# Warnings
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    $<$<CXX_COMPILER_ID:GNU>:-fdiagnostics-color=always>
    $<$<CXX_COMPILER_ID:Clang>:-fcolor-diagnostics -Weverything
        -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-padded
        -Wno-weak-vtables -Wno-unused-member-function>
)

# Hardening
add_library(project_hardening INTERFACE)
target_compile_options(project_hardening INTERFACE
    -fstack-protector-strong
    -fstack-clash-protection
    -fcf-protection=full
    # -D_FORTIFY_SOURCE=3
)
target_link_options(project_hardening INTERFACE
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack)

# Perfomance
add_library(project_performance INTERFACE)
target_compile_options(project_performance INTERFACE
    $<$<CONFIG:Release>: -flto> # -march=native -mtune=native
)
target_link_options(project_performance INTERFACE
    $<$<CONFIG:Release>:-flto>
)

# Sanitizers (NOTE: In debug build `address,undefined` are used by default)
add_library(project_sanitizers INTERFACE)

if(NOT USE_SANITIZER STREQUAL "none")
    set(SAN_FLAGS "-fsanitize=${USE_SANITIZER}")

    target_compile_options(project_sanitizers INTERFACE
        $<$<CONFIG:Debug>:${SAN_FLAGS} -fno-omit-frame-pointer>)
    target_link_options(project_sanitizers INTERFACE
        $<$<CONFIG:Debug>:${SAN_FLAGS}>)
endif()

# All flags in one target `project_options`
add_library(project_options INTERFACE)

target_include_directories(project_options INTERFACE 
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(project_options INTERFACE
    Threads::Threads
    project_hardening
    project_warnings
    project_sanitizers
    project_performance)

# Main Executable
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE project_options)
