if (NOT ENABLE_CLIENT_COMPONENTS)
    return ()
endif ()

#For client-side (remoteoffloadbin), we have dependencies on HddlUnite
find_package(HddlUnite)
if( HddlUnite_FOUND )

  #Build the extension that gets dlopen'ed on the host-side, which
  # is called from the remoteoffloadbin & HDDL Scheduler (HDDLClient.h)
  add_library(remoteoffloadexthddl SHARED
              hddldeviceproxy.cpp
              ../common/xlinkcommschannelcreator.cpp
              ../../common/remoteoffloadcommsio_xlink.c
              remoteoffloadextensionhddl.c)

  target_link_libraries(remoteoffloadexthddl
                        PRIVATE ${GLIBS}
                        ${NAME_REMOTEOFFLOADCORE_LIB}
                        ${xlinkPC_LIBRARY}
                        HddlUnite::HddlUnite)

  set_target_properties(remoteoffloadexthddl
                        PROPERTIES
                        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/remoteoffloadext")

  install( TARGETS remoteoffloadexthddl DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gst-remote-offload/remoteoffloadext")

else()
  message("HddlUnite not found! Skipping build of hddl extension")
endif()



