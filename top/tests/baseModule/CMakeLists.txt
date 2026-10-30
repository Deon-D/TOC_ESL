add_executable(baseModuleTest BaseModuleTest.cpp)
target_link_libraries(baseModuleTest PRIVATE toc::common)
target_compile_options(baseModuleTest PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME baseModuleContract COMMAND baseModuleTest)
