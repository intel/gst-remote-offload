  SET(USE_XLINK 0 )

  #If XLINK_HOME is set, we assume that we are using the simulator
  if(DEFINED ENV{XLINK_HOME})
    FIND_LIBRARY(xlinkARM_LIBRARY NAME XLinkARM PATHS $ENV{XLINK_HOME}/lib DOX "XLink ARM (server) library")
    FIND_LIBRARY(xlinkPC_LIBRARY NAME XLinkPC PATHS $ENV{XLINK_HOME}/lib DOX "XLink PC (host) library")
    SET(USE_XLINK 1 )
    if( xlinkARM_LIBRARY AND xlinkPC_LIBRARY )
      message("XLink Simulator being used.")
      include_directories($ENV{XLINK_HOME}/include)
    else()
      message("Warning! XLINK_HOME env is set, but simulator libraries not found!")
    endif()
  else()
    #In the case of using "Real" XLink, try to find libXLink.so

    find_library(XLINK_LIBRARY NAMES XLink CACHE )
    if( XLINK_LIBRARY )
      message("XLink library found: ${XLINK_LIBRARY}")
      set(xlinkARM_LIBRARY ${XLINK_LIBRARY})
      set(xlinkPC_LIBRARY ${XLINK_LIBRARY})
      SET(USE_XLINK 1 )
    endif()
  endif()

  if( ${USE_XLINK} )
    message("Enabling XLink Extensions")
  else()
    message("Disabling XLink Extensions")
  endif()


