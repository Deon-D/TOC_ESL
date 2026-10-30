add_library(tocMemory STATIC
    src/Memory.cpp
)
add_library(toc::memory ALIAS tocMemory)

target_include_directories(tocMemory PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tocMemory PUBLIC toc::common toc::chiCommon)
target_compile_features(tocMemory PUBLIC cxx_std_17)
target_compile_options(tocMemory PRIVATE -Wall -Wextra -Wpedantic -Werror)
