
include_directories(${GSTREAMER_INCLUDE_DIRS})
include_directories(${GLIB2_INCLUDE_DIRS})
link_directories( ${GSTREAMER_LIBRARY_DIRS} )

add_library( gstremoteoffloadextdummy SHARED
remoteoffloadcommsio_dummy.c
dummydeviceproxy.c
remoteoffloadextensiondummy.c)

target_link_libraries(gstremoteoffloadextdummy ${GLIBS} ${NAME_REMOTEOFFLOADCORE_LIB})

set_target_properties(gstremoteoffloadextdummy
                      PROPERTIES
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/remoteoffloadext")

install( TARGETS gstremoteoffloadextdummy DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gst-remote-offload/remoteoffloadext")

