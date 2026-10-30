cmake_minimum_required(VERSION 3.20)
project(tocEsl LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS "12.2"
   OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS "12.3")
    message(FATAL_ERROR "TOC ESL requires GCC/G++ 12.2.x")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../model ${CMAKE_CURRENT_BINARY_DIR}/model)

add_executable(tocEslSim src/main.cpp)
target_link_libraries(tocEslSim PRIVATE toc::soc)
target_compile_options(tocEslSim PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_subdirectory(tests/baseModule)
add_subdirectory(tests/chiPort)
add_subdirectory(tests/socClock)
add_subdirectory(tests/readSubsystem)
