add_subdirectory(../rct ${CMAKE_CURRENT_BINARY_DIR}/rct)
add_subdirectory(../demux-mpegts ${CMAKE_CURRENT_BINARY_DIR}/demux-mpegts)

set(SOURCES main.cpp)

add_executable(mac ${SOURCES})
target_link_libraries(mac tsDemuxerStatic rct)
