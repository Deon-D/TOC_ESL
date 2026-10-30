cmake_minimum_required(VERSION 3.20)
project(eslToolchainSmoke LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS "12.2"
   OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS "12.3")
    message(FATAL_ERROR "This environment check requires GCC/G++ 12.2.x")
endif()

find_package(Threads REQUIRED)
add_executable(toolchainSmoke main.cpp)
target_compile_features(toolchainSmoke PRIVATE cxx_std_17)
target_link_libraries(toolchainSmoke PRIVATE Threads::Threads)
target_compile_options(toolchainSmoke PRIVATE -Wall -Wextra -Wpedantic)
enable_testing()
add_test(NAME gcc12Cpp17Runtime COMMAND toolchainSmoke)
