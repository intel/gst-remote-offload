include (FindXLink.cmake)
IF( ${USE_XLINK} )

include_directories(common)
include_directories(${GSTREAMER_INCLUDE_DIRS})
include_directories(${GLIB2_INCLUDE_DIRS})
link_directories( ${GSTREAMER_LIBRARY_DIRS} )

add_subdirectory( xlink_ext )
add_subdirectory( hddl_ext )


ENDIF( ${USE_XLINK} )
