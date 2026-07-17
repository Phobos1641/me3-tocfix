cmake_minimum_required(VERSION 3.20)
project(me3tocfix CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Threads REQUIRED)

add_executable(me3tocfix
    src/main.cpp
    src/ini_parser.cpp
    src/toc_bin_file.cpp
    src/toc_creator.cpp
)

target_link_libraries(me3tocfix PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(me3tocfix PRIVATE /W4 /utf-8 /permissive-)
    target_link_libraries(me3tocfix PRIVATE advapi32)
elseif(WIN32)
    # MinGW / Clang-on-Windows
    target_compile_options(me3tocfix PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(me3tocfix PRIVATE advapi32)
else()
    target_compile_options(me3tocfix PRIVATE -Wall -Wextra -Wpedantic)
endif()
