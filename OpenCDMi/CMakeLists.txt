set(PLUGIN_NAME OCDM)

set(WPEFRAMEWORK_PLUGIN_OPENCDMI_AUTOSTART true CACHE STRING true)
set(WPEFRAMEWORK_PLUGIN_OPENCDMI_OOP false CACHE STRING true)

include(module) # Setup default stuff needed for the cmake framework thingy.

set(PLUGIN_OPTIONS)

set(PLUGIN_SOURCES 
        Module.cpp
        OCDM.cpp
        CENCParser.cpp
        FrameworkRPC.cpp)

include(default_targets) # This is a regular plugin no need to edit the defaults

if("${CMAKE_FIND_ROOT_PATH}" STREQUAL "")
   # Desktop case: not cross compiling
   target_link_libraries(WPEFrameworkOCDM "${CMAKE_INSTALL_PREFIX}/lib/libocdm.so")
else()
   # Cross compiling: buildroot
   target_link_libraries(WPEFrameworkOCDM "${CMAKE_FIND_ROOT_PATH}/usr/lib/libocdm.so")
endif()

write_config(${PLUGIN_NAME})
