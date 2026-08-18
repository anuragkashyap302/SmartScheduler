cmake_minimum_required(VERSION 3.12)
project(SmartScheduler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0600)
endif()

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/Task.cpp
    src/TaskQueue.cpp
    src/ThreadPool.cpp
    src/Scheduler.cpp
)

# Main Executable
add_executable(SmartScheduler
    src/main.cpp
    ${SOURCES}
)

# Test Executable
add_executable(SmartSchedulerTests
    tests/tests.cpp
    ${SOURCES}
)

# Enable compiler warnings
if(MSVC)
    target_compile_options(SmartScheduler PRIVATE /W4)
    target_compile_options(SmartSchedulerTests PRIVATE /W4)
else()
    target_compile_options(SmartScheduler PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(SmartSchedulerTests PRIVATE -Wall -Wextra -Wpedantic)
endif()
