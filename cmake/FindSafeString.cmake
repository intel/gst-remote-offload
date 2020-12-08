find_library(SAFESTR_LIBRARY NAMES safestring_static safestring_shared PATHS $ENV{SAFESTR_HOME}/build CACHE )
find_file(SAFESTR_INCLUDE_FILE safe_lib.h PATHS $ENV{SAFESTR_HOME}/include CACHE )

if( SAFESTR_INCLUDE_FILE )
   get_filename_component(SAFESTR_INCLUDE ${SAFESTR_INCLUDE_FILE} DIRECTORY CACHE)
endif()

if( SAFESTR_LIBRARY AND SAFESTR_INCLUDE )
   message("safestring found. Will compile-in safestring support.")
   message("safestring library: ${SAFESTR_LIBRARY}")
   message("safestring include directory: ${SAFESTR_INCLUDE}")

   include_directories (${SAFESTR_INCLUDE})
else()
  SET(CMAKE_C_FLAGS "-DNO_SAFESTR ${CMAKE_C_FLAGS}")
  message("Warning: safestringlib not found. This project will not be built with safestring support.
           To use safestringlib, either install someplace in system path,
           or set SAFESTR_HOME environment variable to root of project.
           i.e. export SAFESTR_HOME=/path/to/safestringlib
           This cmake system will expect to find SAFESTR_HOME/build and SAFESTR_HOME/include")

endif()

