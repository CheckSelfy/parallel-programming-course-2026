cmake_minimum_required(VERSION 3.15)
project(ParallelProgrammingLab1 VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(lab1 
    src/main.cpp
    include/generator.hpp
    include/interface.hpp
    include/phase0.hpp
    include/phase1.hpp
)

target_include_directories(lab1 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(lab1 PRIVATE Threads::Threads)
