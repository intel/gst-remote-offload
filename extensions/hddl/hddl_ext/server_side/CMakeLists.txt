if (NOT ENABLE_SERVER_COMPONENTS)
    return ()
endif ()

#Build the DLL that gets dlopen'ed on the server-side by HDDLUnite
add_library(gstrophddl SHARED
            ../common/xlinkcommschannelcreator.cpp
            ../../common/remoteoffloadcommsio_xlink.c
            gstrophddl.cpp)

target_compile_definitions(gstrophddl PRIVATE XLINKSERVER)

target_link_libraries(gstrophddl
                      PRIVATE ${GLIBS}
                      ${NAME_REMOTEOFFLOADCORE_LIB}
                      ${xlinkARM_LIBRARY})

install( TARGETS gstrophddl
         DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gst-remote-offload/" )


