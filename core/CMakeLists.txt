
set(CORE_LIBRARY_SOURCES
remoteoffloadextension.c
remoteoffloadextregistry.c
remoteoffloaddeviceproxy.c
remoteoffloaddevice.c
remoteoffloadclientserverutil.c
gstremoteoffloadpipeline.c
remoteoffloadpipelinelogger.c
remoteoffloadbinpipelinecommon.c
remoteoffloadcommsio.c
remoteoffloadcomms.c
remoteoffloadprivateinterfaces.c
remoteoffloadcommschannel.c
remoteoffloaddataexchanger.c
remoteoffloadelementserializer.c
remoteoffloadbinserializer.c
remoteoffloadmetaserializer.c
remoteoffloadelementpropertyserializer.c
remoteoffloadutils.c
orderedghashtable.c
exchangers/errormessagedataexchanger.c
exchangers/statechangedataexchanger.c
exchangers/pingdataexchanger.c
exchangers/querydataexchanger.c
exchangers/eosdataexchanger.c
exchangers/bufferdataexchanger.c
exchangers/eventdataexchanger.c
exchangers/queuestatsdataexchanger.c
exchangers/genericdataexchanger.c
exchangers/heartbeatdataexchanger.c
metaserializers/gstvideoroimetaserializer.c
metaserializers/gstvideometaserializer.c
)

set(PUBLIC_HEADERS
gstremoteoffloadpipeline.h
remoteoffloadcommsio.h
remoteoffloadclientserverutil.h
remoteoffloaddeviceproxy.h
remoteoffloaddevice.h
remoteoffloadelementpropertyserializer.h
remoteoffloadelementserializer.h
remoteoffloadextension.h
remoteoffloadmetaserializer.h
remoteoffloadutils.h
)

#create gstremoteoffloadcommon
add_library( ${NAME_REMOTEOFFLOADCORE_LIB} SHARED ${CORE_LIBRARY_SOURCES})
add_library( GstRemoteOffload::${NAME_REMOTEOFFLOADCORE_LIB} ALIAS ${NAME_REMOTEOFFLOADCORE_LIB} )

include(GNUInstallDirs)

target_include_directories( ${NAME_REMOTEOFFLOADCORE_LIB} PUBLIC "$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_INCLUDEDIR}>"
                                                            ${GLIB2_INCLUDE_DIRS}
                                                            ${GSTREAMER_INCLUDE_DIRS}
                                                            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/exchangers
                                                                    ${CMAKE_CURRENT_SOURCE_DIR}/metaserializers
                                                                    ${CMAKE_CURRENT_SOURCE_DIR}/propserializers)

find_library( GLIB2_LIBRARY glib-2.0 PATHS ${GLIB2_LIBRARY_DIRS} )
find_library( GOBJECT2_LIBRARY gobject-2.0 PATHS ${GLIB2_LIBRARY_DIRS} )
find_library( GSTBASE_LIBRARY gstbase-1.0 PATHS ${GSTREAMER_LIBRARY_DIRS} )
find_library( GST_LIBRARY gstreamer-1.0 PATHS ${GSTREAMER_LIBRARY_DIRS} )
find_library( GST_VIDEO_LIBRARY gstvideo-1.0 PATHS ${GSTREAMER_LIBRARY_DIRS} )

target_link_libraries(${NAME_REMOTEOFFLOADCORE_LIB}
   PUBLIC  ${GLIB2_LIBRARY} ${GOBJECT2_LIBRARY} ${GSTBASE_LIBRARY} ${GST_LIBRARY}
   PRIVATE ${GST_VIDEO_LIBRARY})

if( SAFESTR_LIBRARY )
  target_link_libraries(${NAME_REMOTEOFFLOADCORE_LIB} PRIVATE ${SAFESTR_LIBRARY})
endif()


#Install core library & public headers
install( TARGETS ${NAME_REMOTEOFFLOADCORE_LIB}
         EXPORT ${NAME_REMOTEOFFLOADCORE_LIB}-targets
         DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gst-remote-offload" )
install( FILES ${PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/GstRemoteOffload)

set_target_properties(${NAME_REMOTEOFFLOADCORE_LIB} PROPERTIES EXPORT_NAME GstRemoteOffload)

#Export the targets to a script
install(EXPORT ${NAME_REMOTEOFFLOADCORE_LIB}-targets
  FILE
    GstRemoteOffloadTargets.cmake
  NAMESPACE
    GstRemoteOffload::
  DESTINATION
    ${INSTALL_CONFIGDIR}
)

#Create a ConfigVersion.cmake file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/GstRemoteOffloadConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(${CMAKE_CURRENT_LIST_DIR}/cmake/GstRemoteOffloadConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/GstRemoteOffloadConfig.cmake
    INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

#Install the config, configversion and custom find modules
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/GstRemoteOffloadConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/GstRemoteOffloadConfigVersion.cmake
    DESTINATION ${INSTALL_CONFIGDIR}
)

export(EXPORT ${NAME_REMOTEOFFLOADCORE_LIB}-targets FILE ${CMAKE_CURRENT_BINARY_DIR}/GstRemoteOffloadTargets.cmake NAMESPACE JSONUtils::)

export(PACKAGE GstRemoteOffload)
