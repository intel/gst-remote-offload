include_directories(${GSTREAMER_INCLUDE_DIRS})
include_directories(${GLIB2_INCLUDE_DIRS})
link_directories( ${GSTREAMER_LIBRARY_DIRS} )

set( GVA_EXT_SOURCES
     gvaelementpropertyserializer.c
     remoteoffloadextensiongva.c)

IF( ${USE_GVA} )
 set( GVA_EXT_SOURCES
 gvajsonmetaserializer.c
 gvatensormetaserializer.c
 ${GVA_EXT_SOURCES})
ENDIF( ${USE_GVA} )

IF( ${USE_GVA_AUDIO} )
 set( GVA_EXT_SOURCES
 gvaaudioeventmetaserializer.c
 ${GVA_EXT_SOURCES})
ENDIF( ${USE_GVA_AUDIO} )

add_library( remoteoffloadextgva SHARED ${GVA_EXT_SOURCES} )

target_link_libraries(remoteoffloadextgva ${GLIBS} ${NAME_REMOTEOFFLOADCORE_LIB} )

IF( ${USE_GVA} )
   target_link_libraries(remoteoffloadextgva ${gstvideoanalyticsmeta_LIBRARY} )
ENDIF( ${USE_GVA} )

if( SAFESTR_LIBRARY )
  target_link_libraries(remoteoffloadextgva ${SAFESTR_LIBRARY})
endif()

set_target_properties(remoteoffloadextgva
                      PROPERTIES
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/remoteoffloadext")

install( TARGETS remoteoffloadextgva DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gst-remote-offload/remoteoffloadext")


