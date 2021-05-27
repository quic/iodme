add_executable(dummy-test dummy.cc)
target_link_libraries(dummy-test boost_program_options iodme)
add_test(NAME dummy COMMAND dummy-test)
