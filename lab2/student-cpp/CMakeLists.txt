cmake_minimum_required(VERSION 3.15)
project(ParallelProgrammingLab2 VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(lab2
    src/main.cpp
    src/scheduler.cpp
)

target_include_directories(lab2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(lab2 PRIVATE Threads::Threads)

foreach(EXE stress ministress)
    add_executable(${EXE} extra/${EXE}.cpp)
    target_include_directories(${EXE} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${EXE} PRIVATE Threads::Threads)
endforeach()