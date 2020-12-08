#Autonomous-mode components are only needed on server-side.
if (NOT ENABLE_SERVER_COMPONENTS)
    return ()
endif ()

include (FindXLink.cmake)
if( NOT ${USE_XLINK} )
   message("XLink not found. Disabling autonomous-mode extensions")
   return ()
endif()

include_directories (common)

include_directories(${GSTREAMER_INCLUDE_DIRS})
link_directories(${GSTREAMER_LIBRARY_DIRS})

set (HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlcontext.h"
                  "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlconn.h"
                  "${CMAKE_CURRENT_SOURCE_DIR}/gsthddltcpconn.h"
                  "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlxlinkcomms.h")

set (HEADER_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

set (SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlcontext.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlxlinkcomms.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gsthddltcpplaceholder.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlconn.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlutils.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gsthddl.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlsrc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlsink.c"
     "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlutils.c")

if ( NOT ${USE_XLINK} )
   set (SOURCE ${SOURCE}
       "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlxlinkplaceholders.c")
endif()

set(HDDLSRC_SINK_USE_GVA ${USE_GVA})

if( ${HDDLSRC_SINK_USE_GVA} )
  add_definitions( -DGVA_PLUGIN )
  set(SOURCE ${SOURCE}
     "${CMAKE_CURRENT_SOURCE_DIR}/gsthddlvideoroimeta.c")
endif()

add_library( gsthddl SHARED ${SOURCE})
target_link_libraries(gsthddl ${GLIBS})
if( SAFESTR_LIBRARY )
  target_link_libraries(gsthddl ${SAFESTR_LIBRARY})
endif()
if (USE_XLINK)
  target_link_libraries(gsthddl ${xlinkPC_LIBRARY})
endif()

if( ${HDDLSRC_SINK_USE_GVA} )
  target_link_libraries(gsthddl ${gstvideoanalyticsmeta_LIBRARY})
endif()

set_target_properties (gsthddl PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                       "${CMAKE_BINARY_DIR}/gstreamer-1.0")
install( TARGETS gsthddl DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gstreamer-1.0" )
install (FILES ${HEADER_FILES}
        DESTINATION ${HEADER_INSTALL_PREFIX}/include/hddl)

