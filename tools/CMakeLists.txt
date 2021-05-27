add_executable(iodme-file-write iodme-file-write.cc)
target_link_libraries(iodme-file-write boost_program_options iodme)

add_executable(iodme-generator iodme-generator.cc)
target_link_libraries(iodme-generator boost_program_options iodme)

add_executable(iodme-sink iodme-sink.cc)
target_link_libraries(iodme-sink boost_program_options iodme)
