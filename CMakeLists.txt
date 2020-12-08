cmake_minimum_required(VERSION 3.5)
project(GstRemoteOffload VERSION 0.5.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

include(CTest)

set(CMAKE_C_STANDARD 99)

#try to get GStreamer components that are required for the sample
find_package(PkgConfig)
pkg_check_modules(GLIB2 glib-2.0)
pkg_check_modules(GSTREAMER gstreamer-1.0)

if(NOT GLIB2_FOUND OR NOT GSTREAMER_FOUND )
    message(WARNING "GstRemoteOffload project is not configured due to absence of gstreamer component(s)" )
    message(WARNING "Please install GStreamer 1.14.0+ before running the sample." )
    return()
endif()

#GStreamer

set(GLIBS ${GLIBS} glib-2.0 gobject-2.0 gio-2.0 gstcontroller-1.0 gstbase-1.0 gstreamer-1.0 gstvideo-1.0)

include (${CMAKE_SOURCE_DIR}/cmake/FindGVAPlugin.cmake)
include (${CMAKE_SOURCE_DIR}/cmake/FindSafeString.cmake)


if (CMAKE_C_COMPILER_VERSION VERSION_GREATER 4.9)
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -std=gnu99 -Wall -fPIE -fstack-protector-strong")
else ()
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -std=gnu99 -Wall -fPIE -fstack-protector")
endif ()

if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 4.9)
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall -fPIE -fstack-protector-strong")
else ()
	set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall -fPIE -fstack-protector")
endif ()

set (CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -D_FORTIFY_SOURCE=2 -O2")
set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS} -g -ggdb -O0")
set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -D_FORTIFY_SOURCE=2 -O2")
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} -g -ggdb -O0")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -z noexecstack -z relro -z now")

#dump all build collateral to the root of the build directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(NAME_REMOTEOFFLOADCORE_LIB gstremoteoffloadcore)

option (ENABLE_CLIENT_COMPONENTS "Enable the build of client-specific components" ON)
option (ENABLE_SERVER_COMPONENTS "Enable the build of server applications & server-specific libraries" ON)
option (ENABLE_TESTS "Enable the build of test applications" ON)

message( "ENABLE_CLIENT_COMPONENTS = ${ENABLE_CLIENT_COMPONENTS}")
message( "ENABLE_SERVER_COMPONENTS = ${ENABLE_SERVER_COMPONENTS}")
message( "ENABLE_TESTS = ${ENABLE_TESTS}")


include_directories( core )
include_directories( core/exchangers )
add_subdirectory( core )
add_subdirectory( extensions )
add_subdirectory( gstremoteoffloadplugin )
add_subdirectory( gsthelperelements )
add_subdirectory( tests )

#create & install setup script for user convenience
file(WRITE ${CMAKE_BINARY_DIR}/install_script_gen/setup_env.sh
"export GST_PLUGIN_PATH=${CMAKE_INSTALL_PREFIX}/lib/gstreamer-1.0:\${GST_PLUGIN_PATH}
export PATH=${CMAKE_INSTALL_PREFIX}/bin:\${PATH}
export LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib/gst-remote-offload/:\${LD_LIBRARY_PATH}
export GST_REMOTEOFFLOAD_PLUGIN_PATH=${CMAKE_INSTALL_PREFIX}/lib/gst-remote-offload/remoteoffloadext/:\${GST_REMOTEOFFLOAD_PLUGIN_PATH}
"
)

install( FILES ${CMAKE_BINARY_DIR}/install_script_gen/setup_env.sh
         DESTINATION ${CMAKE_INSTALL_PREFIX}/share/gst-remote-offload/scripts )



