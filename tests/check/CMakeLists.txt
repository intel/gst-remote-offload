set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/check/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/check/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/check/)

include_directories(${GSTREAMER_INCLUDE_DIRS})
include_directories(${GLIB2_INCLUDE_DIRS})
link_directories( ${GSTREAMER_LIBRARY_DIRS} )

enable_testing()

add_library( remoteoffloadtestutils SHARED
appsinkcomparer.c
robtestutils.c
)

target_link_libraries( remoteoffloadtestutils
                       ${GLIBS}
                       ${NAME_REMOTEOFFLOADCORE_LIB}
                       gstcheck-1.0
                       gstapp-1.0)

if( SAFESTR_LIBRARY )
  target_link_libraries(remoteoffloadtestutils ${SAFESTR_LIBRARY})
endif()


ADD_EXECUTABLE( rob_basic rob_basic.c )
target_link_libraries(rob_basic ${GLIBS} remoteoffloadtestutils)
ADD_TEST( rob_basic rob_basic )

ADD_EXECUTABLE( sublaunch sublaunch.c )
target_link_libraries(sublaunch ${GLIBS} remoteoffloadtestutils)
ADD_TEST( sublaunch sublaunch )

ADD_EXECUTABLE( rob_error_handling rob_error_handling.c )
target_link_libraries(rob_error_handling ${GLIBS} remoteoffloadtestutils)
ADD_TEST( rob_error_handling rob_error_handling )

ADD_EXECUTABLE( videoroimetafilter videoroimetafilter.c )
target_link_libraries(videoroimetafilter ${GLIBS} remoteoffloadtestutils)
ADD_TEST( videoroimetafilter videoroimetafilter )
