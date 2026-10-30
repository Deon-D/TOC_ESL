add_library(tocCommon STATIC
    src/BaseModule.cpp
)
add_library(toc::common ALIAS tocCommon)

target_include_directories(tocCommon PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tocCommon PUBLIC toc::socEnv)
target_compile_features(tocCommon PUBLIC cxx_std_17)
target_compile_options(tocCommon PRIVATE -Wall -Wextra -Wpedantic -Werror)
