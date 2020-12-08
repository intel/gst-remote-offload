
include_directories(${GSTREAMER_INCLUDE_DIRS})
include_directories(${GLIB2_INCLUDE_DIRS})
link_directories( ${GSTREAMER_LIBRARY_DIRS} )

#build the "helper" GstElements. Technically, these have nothing to do with the
# 'remoteoffload' capabilities. They are simply custom elements that are used
# for some of the tests/examples to "show off" the capabilities of the
# remote offload framework.
include_directories(gsthelperelements)
add_library(gstvideoroimetadetach SHARED
gstvideoroimetadetach.c
)
target_link_libraries(gstvideoroimetadetach ${GLIBS} )


add_library(gstvideoroimetaattach SHARED
gstvideoroimetaattach.c
)
target_link_libraries(gstvideoroimetaattach ${GLIBS} )


add_library(gstvideoroicrop SHARED
gstvideoroicrop.c
gstvideoroicropinternal.c
)
target_link_libraries(gstvideoroicrop ${GLIBS} )

add_library(gstvideoroicompose SHARED
gstvideoroicompose.c
)
target_link_libraries(gstvideoroicompose ${GLIBS} )

add_library(gstbufferspersecond SHARED
gstbufferspersecond.c
)
target_link_libraries(gstbufferspersecond ${GLIBS} )

add_library(gstsublaunch SHARED
gstsublaunch.c
)
target_link_libraries(gstsublaunch ${GLIBS} )

add_library(gstvideoroimetafilter SHARED
gstvideoroimetafilter.c
)
target_link_libraries(gstvideoroimetafilter ${GLIBS} )

set_target_properties(gstvideoroimetadetach
                      gstvideoroimetaattach
                      gstvideoroicrop
                      gstvideoroicompose
                      gstbufferspersecond
                      gstsublaunch
                      gstvideoroimetafilter
                      PROPERTIES
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gstreamer-1.0")

install( TARGETS gstvideoroimetadetach
                 gstvideoroimetaattach
                 gstvideoroicrop
                 gstvideoroicompose
                 gstbufferspersecond
                 gstsublaunch
                 gstvideoroimetafilter
                 DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gstreamer-1.0")
