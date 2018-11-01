# - Try to find gstmpegts
# Once done this will define
#  GSTMPEGTS_FOUND - System has gstmpegts
#  GSTMPEGTS_INCLUDE_DIR - The libgstmpegts include directories
#  GSTMPEGTS_LIBRARIES - The libraries needed to use libgstmpegts

find_library(
    GSTMPEGTS_LIB
    NAMES gstmpegts-1.0
    HINTS /usr/lib /usr/local/lib ${CMAKE_INSTALL_PREFIX}/usr/lib
    PATH_SUFFIXES gstmpegts-1.0)

if("${GSTMPEGTS_INCLUDE_DIR}" STREQUAL "" OR "${GSTMPEGTS_LIB}" STREQUAL "")
    set(GSTMPEGTS_FOUND_TEXT "Not found")
else()
    set(GSTMPEGTS_FOUND_TEXT "Found")
endif()

if (WPEFRAMEWORK_VERBOSE_BUILD)
    message(STATUS "gstmpegts       : ${GSTMPEGTS_FOUND_TEXT}")
    message(STATUS "  version      : ${PC_GSTMPEGTS_VERSION}")
    message(STATUS "  cflags       : ${PC_GSTMPEGTS_CFLAGS}")
    message(STATUS "  cflags other : ${PC_GSTMPEGTS_CFLAGS_OTHER}")
    message(STATUS "  libs         : ${GSTMPEGTS_LIB}")
endif()

set(GSTMPEGTS_DEFINITIONS ${PC_GSTMPEGTS_PLUGINS_CFLAGS_OTHER})
set(GSTMPEGTS_LIBRARIES ${GSTMPEGTS_LIB})
set(GSTMPEGTS_LIBRARY_DIRS ${PC_GSTMPEGTS_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GSTMPEGTS DEFAULT_MSG
    GSTMPEGTS_LIBRARIES )

if(GSTMPEGTS_FOUND)
    message(WARNING "got libgstmpegts.")
else()
    message(WARNING "Could not find libgstmpegts")
endif()

mark_as_advanced(GSTMPEGTS_DEFINITIONS GSTMPEGTS_LIBRARIES)
