
#build the gstremoteoffload plugin.
include_directories(gstremoteoffloadplugin)
set(NAME_GST_LIB gstremoteoffload)

include_directories(${GSTREAMER_INCLUDE_DIRS})
include_directories(${GLIB2_INCLUDE_DIRS})
link_directories( ${GSTREAMER_LIBRARY_DIRS} )

set(GST_REMOTEOFFLOAD_PLUGIN_SOURCES gstremoteoffloadegress.c gstremoteoffloadingress.c)

if (ENABLE_CLIENT_COMPONENTS)
    set(GST_REMOTEOFFLOAD_PLUGIN_SOURCES ${GST_REMOTEOFFLOAD_PLUGIN_SOURCES} gstremoteoffloadbin.c)
    add_definitions( -DENABLE_REMOTEOFFLOADBIN )
endif ()

add_library(${NAME_GST_LIB} SHARED ${GST_REMOTEOFFLOAD_PLUGIN_SOURCES}
plugin.c
)

target_link_libraries(${NAME_GST_LIB} ${GLIBS} ${NAME_REMOTEOFFLOADCORE_LIB} )

set_target_properties(${NAME_GST_LIB}
                      PROPERTIES
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gstreamer-1.0")

install( TARGETS ${NAME_GST_LIB} DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gstreamer-1.0")
