cmake_minimum_required(VERSION 3.28)
project(udp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
enable_testing()

add_executable(udp src/main.cpp)
target_compile_options(udp PRIVATE -Wall -Wextra)
target_sources(udp
    PRIVATE
        FILE_SET CXX_MODULES
        FILES 
            src/udp.cppm
            src/udp_representation.cppm
            src/checksum.cppm
            src/package.cppm
            src/utils.cppm
)
