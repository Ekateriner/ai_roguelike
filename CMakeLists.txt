cmake_minimum_required(VERSION 3.13)

project(ai)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(hw1 "Build first homework" OFF)
option(hw2 "Build second homework" OFF)
option(hw3 "Build third homework" OFF)
option(hw4 "Build 4th homework" OFF)
option(hw5 "Build 5th homework" OFF)
option(hw6 "Build 6th homework" OFF)
option(hw7 "Build 7th homework" ON)
option(hw8 "Build 8th homework" ON)

add_library(project_options INTERFACE)
add_library(project_warnings INTERFACE)

include(cmake/CompilerWarnings.cmake)
set_project_warnings(project_warnings)

include(cmake/Sanitizers.cmake)
enable_sanitizers(project_options)

add_subdirectory(3rdParty)

if (hw1)
    add_subdirectory(w1)
endif()

if (hw2)
    add_subdirectory(w2)
endif()

if (hw3)
    add_subdirectory(w3)
endif()

if (hw4)
    add_subdirectory(w4)
endif()

if (hw5)
    add_subdirectory(w5)
endif()

if (hw6)
    add_subdirectory(w6)
endif()

if (hw7)
    add_subdirectory(w7)
endif()

if (hw8)
    add_subdirectory(w8)
endif()