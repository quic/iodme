set(IODME_HPP
	${PROJECT_SOURCE_DIR}/include/iodme/timesource.hpp
	${PROJECT_SOURCE_DIR}/include/iodme/buffer.hpp
	${PROJECT_SOURCE_DIR}/include/iodme/thread.hpp
	${PROJECT_SOURCE_DIR}/include/iodme/queue.hpp
	${PROJECT_SOURCE_DIR}/include/iodme/file-writer.hpp
	${PROJECT_SOURCE_DIR}/include/iodme/mover.hpp
	${PROJECT_SOURCE_DIR}/include/iodme/netrx.hpp
	${PROJECT_SOURCE_DIR}/include/iodme/nettx.hpp
	${PROJECT_SOURCE_DIR}/include/iodme/pump.hpp)

add_library(iodme SHARED ${IODME_HPP}
	buffer.cc
	thread.cc
	file-writer.cc
	mover.cc
	netrx.cc
	nettx.cc
	pump.cc)

target_include_directories(iodme PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(iodme PUBLIC hogl)

set_target_properties(iodme PROPERTIES VERSION ${IODME_VERSION})

install(TARGETS iodme DESTINATION lib COMPONENT dev)
install(FILES ${IODME_HPP} DESTINATION include/iodme COMPONENT dev)
