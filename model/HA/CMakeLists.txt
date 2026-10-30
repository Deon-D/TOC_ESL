add_library(tocHa STATIC
    src/HA.cpp
)
add_library(toc::ha ALIAS tocHa)

target_include_directories(tocHa PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tocHa PUBLIC toc::common toc::chiCommon)
target_compile_features(tocHa PUBLIC cxx_std_17)
target_compile_options(tocHa PRIVATE -Wall -Wextra -Wpedantic -Werror)
