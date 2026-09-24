########## MACROS ###########################################################################
#############################################################################################

# Requires CMake > 3.15
if(${CMAKE_VERSION} VERSION_LESS "3.15")
    message(FATAL_ERROR "The 'CMakeDeps' generator only works with CMake >= 3.15")
endif()

if(tinyxml2_FIND_QUIETLY)
    set(tinyxml2_MESSAGE_MODE VERBOSE)
else()
    set(tinyxml2_MESSAGE_MODE STATUS)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmakedeps_macros.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tinyxml2Targets.cmake)
include(CMakeFindDependencyMacro)

check_build_type_defined()

foreach(_DEPENDENCY ${tinyxml2_FIND_DEPENDENCY_NAMES} )
    # Check that we have not already called a find_package with the transitive dependency
    if(NOT ${_DEPENDENCY}_FOUND)
        find_dependency(${_DEPENDENCY} REQUIRED ${${_DEPENDENCY}_FIND_MODE})
    endif()
endforeach()

set(tinyxml2_VERSION_STRING "10.0.0")
set(tinyxml2_INCLUDE_DIRS ${tinyxml2_INCLUDE_DIRS_RELEASE} )
set(tinyxml2_INCLUDE_DIR ${tinyxml2_INCLUDE_DIRS_RELEASE} )
set(tinyxml2_LIBRARIES ${tinyxml2_LIBRARIES_RELEASE} )
set(tinyxml2_DEFINITIONS ${tinyxml2_DEFINITIONS_RELEASE} )


# Definition of extra CMake variables from cmake_extra_variables


# Only the last installed configuration BUILD_MODULES are included to avoid the collision
foreach(_BUILD_MODULE ${tinyxml2_BUILD_MODULES_PATHS_RELEASE} )
    message(${tinyxml2_MESSAGE_MODE} "Conan: Including build module from '${_BUILD_MODULE}'")
    include(${_BUILD_MODULE})
endforeach()


