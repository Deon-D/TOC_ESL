add_executable(chiPortTest CHIPortTest.cpp)
target_link_libraries(chiPortTest PRIVATE toc::chiCommon)
target_compile_options(chiPortTest PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME chiPortContract COMMAND chiPortTest)
