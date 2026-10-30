add_library(tocChiCommon STATIC
    src/ChiCommon.cpp
    src/CHIPort.cpp
)
add_library(toc::chiCommon ALIAS tocChiCommon)

target_include_directories(tocChiCommon PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(tocChiCommon PUBLIC cxx_std_17)
target_compile_options(tocChiCommon PRIVATE -Wall -Wextra -Wpedantic -Werror)
