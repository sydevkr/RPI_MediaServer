cmake_minimum_required(VERSION 3.16)
project(RPI_MediaServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED 
    libavcodec>=58.0
    libavformat>=58.0
    libavutil>=56.0
    libswscale>=5.0
    libavfilter>=7.0
    libavdevice>=58.0
)

# Hailo (optional)
find_library(HAILORT_LIB hailort)
find_path(HAILORT_INCLUDE hailort.h PATH_SUFFIXES hailort)

if(HAILORT_LIB AND HAILORT_INCLUDE)
    message(STATUS "HailoRT found: ${HAILORT_LIB}")
    add_definitions(-DHAS_HAILORT=1)
else()
    message(WARNING "HailoRT not found, building without AI support")
    set(HAILORT_LIB "")
    set(HAILORT_INCLUDE "")
endif()

# Source files
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    src/main.cpp
    src/pipeline/*.cpp
    src/ai/*.cpp
    src/utils/*.cpp
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
    ${FFMPEG_INCLUDE_DIRS}
    ${HAILORT_INCLUDE}
    src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${FFMPEG_LIBRARIES}
    ${HAILORT_LIB}
    pthread
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${FFMPEG_CFLAGS_OTHER}
    -Wall -Wextra -O3
)
