add_executable(socClockTest SoCClockTest.cpp)
target_link_libraries(socClockTest PRIVATE toc::common toc::chiCommon)
target_compile_options(socClockTest PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_test(NAME socClockContract COMMAND socClockTest)
