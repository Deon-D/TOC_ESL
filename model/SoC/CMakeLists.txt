add_library(tocSoC STATIC
    src/SoC.cpp
)
add_library(toc::soc ALIAS tocSoC)

target_include_directories(tocSoC PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tocSoC PUBLIC toc::common toc::rn toc::memory)
target_compile_features(tocSoC PUBLIC cxx_std_17)
target_compile_options(tocSoC PRIVATE -Wall -Wextra -Wpedantic -Werror)
