# - Try to find JACK
# Once done this will define
#  JACK_FOUND - System has JACK
#  JACK_INCLUDE_DIRS - The JACK include directory
#  JACK_LIBRARIES - The library needed to use JACK
# An imported target JACK::JACK is also created, prefer this

include(FindPkgConfig)
pkg_check_modules(jack_PKG QUIET jack)

find_path(JACK_INCLUDE_DIRS
  NAMES
    jack/jack.h
  PATHS
    ${jack_PKG_INCLUDE_DIRS}
    /usr/include/jack
    /usr/include
    /usr/local/include/jack
    /usr/local/include
)
find_library(JACK_LIBRARIES
  NAMES
    libjack jack
  PATHS
    ${jack_PKG_LIBRARY_DIRS}
    /usr/lib
    /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JACK DEFAULT_MSG JACK_LIBRARIES JACK_INCLUDE_DIRS)

if(JACK_FOUND)
  if(NOT TARGET JACK::JACK)
    add_library(JACK::JACK UNKNOWN IMPORTED)
    set_target_properties(JACK::JACK PROPERTIES
      IMPORTED_LOCATION ${JACK_LIBRARIES}
      INTERFACE_INCLUDE_DIRECTORIES ${JACK_INCLUDE_DIRS}
    )
  endif()
endif()

mark_as_advanced(JACK_INCLUDE_DIRS JACK_LIBRARIES)
