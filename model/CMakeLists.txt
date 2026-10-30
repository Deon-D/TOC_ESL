add_library(tocSocEnv INTERFACE)
add_library(toc::socEnv ALIAS tocSocEnv)
target_include_directories(tocSocEnv INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/SoC/include>
)
target_compile_features(tocSocEnv INTERFACE cxx_std_17)

add_subdirectory(Common)
add_subdirectory(ChiCommon)
add_subdirectory(RN)
add_subdirectory(Memory)
add_subdirectory(SoC)
