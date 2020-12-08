if( NOT ENABLE_TESTS )
  return()
endif()

add_subdirectory(remoteoffloadbin)
add_subdirectory(autonomous_mode)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    add_subdirectory(check)
endif()

