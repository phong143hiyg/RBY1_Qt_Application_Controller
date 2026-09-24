########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

set(tinyxml2_COMPONENT_NAMES "")
if(DEFINED tinyxml2_FIND_DEPENDENCY_NAMES)
  list(APPEND tinyxml2_FIND_DEPENDENCY_NAMES )
  list(REMOVE_DUPLICATES tinyxml2_FIND_DEPENDENCY_NAMES)
else()
  set(tinyxml2_FIND_DEPENDENCY_NAMES )
endif()

########### VARIABLES #######################################################################
#############################################################################################
set(tinyxml2_PACKAGE_FOLDER_RELEASE "C:/Users/ADMIN/source/repos/RBY1DesktopQt_Moc4_StatePattern/.dependencies/conan-home/p/b/tinyx106c015904130/p")
set(tinyxml2_BUILD_MODULES_PATHS_RELEASE )


set(tinyxml2_INCLUDE_DIRS_RELEASE "${tinyxml2_PACKAGE_FOLDER_RELEASE}/include")
set(tinyxml2_RES_DIRS_RELEASE )
set(tinyxml2_DEFINITIONS_RELEASE )
set(tinyxml2_SHARED_LINK_FLAGS_RELEASE )
set(tinyxml2_EXE_LINK_FLAGS_RELEASE )
set(tinyxml2_OBJECTS_RELEASE )
set(tinyxml2_COMPILE_DEFINITIONS_RELEASE )
set(tinyxml2_COMPILE_OPTIONS_C_RELEASE )
set(tinyxml2_COMPILE_OPTIONS_CXX_RELEASE )
set(tinyxml2_LIB_DIRS_RELEASE "${tinyxml2_PACKAGE_FOLDER_RELEASE}/lib")
set(tinyxml2_BIN_DIRS_RELEASE )
set(tinyxml2_LIBRARY_TYPE_RELEASE STATIC)
set(tinyxml2_IS_HOST_WINDOWS_RELEASE 1)
set(tinyxml2_LIBS_RELEASE tinyxml2)
set(tinyxml2_SYSTEM_LIBS_RELEASE )
set(tinyxml2_FRAMEWORK_DIRS_RELEASE )
set(tinyxml2_FRAMEWORKS_RELEASE )
set(tinyxml2_BUILD_DIRS_RELEASE )
set(tinyxml2_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(tinyxml2_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${tinyxml2_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${tinyxml2_COMPILE_OPTIONS_C_RELEASE}>")
set(tinyxml2_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${tinyxml2_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${tinyxml2_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${tinyxml2_EXE_LINK_FLAGS_RELEASE}>")


set(tinyxml2_COMPONENTS_RELEASE )