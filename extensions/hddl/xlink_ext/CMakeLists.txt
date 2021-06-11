
if (ENABLE_CLIENT_COMPONENTS)
  add_library( remoteoffloadextxlink SHARED
    ../common/remoteoffloadcommsio_xlink.c
    xlinkchannelmanager.c
    xlinkdeviceproxy.c
    remoteoffloadextensionxlink.c
  )
  target_link_libraries(remoteoffloadextxlink ${GLIBS} ${NAME_REMOTEOFFLOADCORE_LIB} ${xlinkPC_LIBRARY} )

  if( SAFESTR_LIBRARY )
    target_link_libraries(remoteoffloadextxlink ${SAFESTR_LIBRARY})
  endif()

  set_target_properties(remoteoffloadextxlink
                        PROPERTIES
                        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/remoteoffloadext")

  install( TARGETS remoteoffloadextxlink DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gst-remote-offload/remoteoffloadext")
endif ()

if (ENABLE_SERVER_COMPONENTS)
  add_executable(gst_offload_xlink_server
    ../common/remoteoffloadcommsio_xlink.c
    xlinkchannelmanager.c
    gst_offload_xlink_server.c
  )
  target_compile_definitions(gst_offload_xlink_server PRIVATE XLINKSERVER)
  target_link_libraries(gst_offload_xlink_server ${GLIBS} ${NAME_REMOTEOFFLOADCORE_LIB} ${xlinkARM_LIBRARY})

  if( SAFESTR_LIBRARY )
    target_link_libraries(gst_offload_xlink_server ${SAFESTR_LIBRARY})
  endif()

  install( TARGETS gst_offload_xlink_server DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif ()



