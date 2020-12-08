if (NOT ENABLE_SERVER_COMPONENTS)
    return ()
endif ()

include (${CMAKE_HOME_DIRECTORY}/extensions/autonomous_mode/FindXLink.cmake)
if (${USE_XLINK})
    message ("Build Autonomous Mode samples")

    set (HDDL_INCLUDE_DIR ${CMAKE_HOME_DIRECTORY}/extensions/autonomous_mode/)
    include_directories(${HDDL_INCLUDE_DIR})
    include_directories(${GSTREAMER_INCLUDE_DIRS})
    link_directories(${GSTREAMER_LIBRARY_DIRS} )

    set (LINK_LIBS pthread gsthddl)

    add_executable(target_passthrough_app target_passthrough_app.c)
    target_link_libraries(target_passthrough_app ${LINK_LIBS} )

    add_executable(target_dec_app target_dec_app.c)
    target_link_libraries(target_dec_app ${LINK_LIBS} )

    add_executable(target_trans_app target_trans_app.c)
    target_link_libraries(target_trans_app ${LINK_LIBS} )
else ()
    message ("XLink not found. Skip building Autonomous Mode samples")
endif (${USE_XLINK})
