add_executable(readSubsystemTest ReadSubsystemTest.cpp)
target_link_libraries(readSubsystemTest PRIVATE toc::soc)
target_compile_options(readSubsystemTest PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME readSubsystemContract COMMAND readSubsystemTest)
