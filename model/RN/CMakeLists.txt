add_library(tocRn STATIC
    src/RN.cpp
)
add_library(toc::rn ALIAS tocRn)

target_include_directories(tocRn PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tocRn PUBLIC toc::common toc::chiCommon)
target_compile_features(tocRn PUBLIC cxx_std_17)
target_compile_options(tocRn PRIVATE -Wall -Wextra -Wpedantic -Werror)
