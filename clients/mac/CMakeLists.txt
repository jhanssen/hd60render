add_subdirectory(../../rct ${CMAKE_CURRENT_BINARY_DIR}/rct)
add_subdirectory(../../demux-mpegts ${CMAKE_CURRENT_BINARY_DIR}/demux-mpegts)

include(ExternalProject)

externalproject_add(
    faad2build
    DOWNLOAD_COMMAND ""
    SOURCE_DIR ../../faad2
    CONFIGURE_COMMAND ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/faad2 --enable-shared=no --with-xmms=no --with-mpeg4ip=no
    BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/faad2/lib/libfaad.a
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND make install
    )

add_library(faad2 STATIC IMPORTED)
set_target_properties(faad2 PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/faad2/lib/libfaad.a)
add_dependencies(faad2 faad2build)

set(SOURCES main.cpp Renderer.cpp Demuxer.cpp View.mm AAC.cpp Log.cpp h264_bit_reader.cc h264_parser.cc)
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -std=c++17)

find_library(FOUNDATION_LIBRARY Foundation)
find_library(APPKIT_LIBRARY AppKit)
find_library(OPENGL_LIBRARY OpenGL)
find_library(IOSURFACE_LIBRARY IOSurface)
find_library(VIDEOTOOLBOX_LIBRARY VideoToolbox)
find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
find_library(COREFOUNDATION_LIBRARY CoreFoundation)
find_library(COREMEDIA_LIBRARY CoreMedia)
find_library(COREVIDEO_LIBRARY CoreVideo)
mark_as_advanced(VIDEOTOOLBOX_LIBRARY
    AUDIOTOOLBOX_LIBRARY
    COREFOUNDATION_LIBRARY
    COREMEDIA_LIBRARY
    COREVIDEO_LIBRARY)
find_path(VIDEOTOOLBOX_INCLUDE_DIR VideoToolbox.h)
find_path(AUDIOTOOLBOX_INCLUDE_DIR AudioToolbox.h)
find_path(COREVIDEO_INCLUDE_DIR CVImageBuffer.h)
list(APPEND MACLIBS
    ${FOUNDATION_LIBRARY}
    ${APPKIT_LIBRARY}
    ${OPENGL_LIBRARY}
    ${IOSURFACE_LIBRARY}
    ${VIDEOTOOLBOX_LIBRARY}
    ${AUDIOTOOLBOX_LIBRARY}
    ${COREFOUNDATION_LIBRARY}
    ${COREMEDIA_LIBRARY}
    ${COREVIDEO_LIBRARY}
    )

include_directories(../../rct rct/include ../../demux-mpegts/src faad2/include
    ${VIDEOTOOLBOX_INCLUDE_DIR}
    ${AUDIOTOOLBOX_INCLUDE_DIR}
    ${COREVIDEO_INCLUDE_DIR})

add_executable(mac ${SOURCES})
target_link_libraries(mac tsDemuxerStatic faad2 rct ${MACLIBS})
