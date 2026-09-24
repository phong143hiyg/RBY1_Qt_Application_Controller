# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\RBY1DesktopQt_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\RBY1DesktopQt_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\RBY1PlanningTests_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\RBY1PlanningTests_autogen.dir\\ParseCache.txt"
  "CMakeFiles\\RBY1SdkTests_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\RBY1SdkTests_autogen.dir\\ParseCache.txt"
  "RBY1DesktopQt_autogen"
  "RBY1PlanningTests_autogen"
  "RBY1SdkTests_autogen"
  "_deps\\qdldl-build\\CMakeFiles\\qdldlobject_autogen.dir\\AutogenUsed.txt"
  "_deps\\qdldl-build\\CMakeFiles\\qdldlobject_autogen.dir\\ParseCache.txt"
  "_deps\\qdldl-build\\qdldlobject_autogen"
  "rby1-sdk\\CMakeFiles\\proto-objects_autogen.dir\\AutogenUsed.txt"
  "rby1-sdk\\CMakeFiles\\proto-objects_autogen.dir\\ParseCache.txt"
  "rby1-sdk\\proto-objects_autogen"
  "rby1-sdk\\src\\CMakeFiles\\rby1-sdk_autogen.dir\\AutogenUsed.txt"
  "rby1-sdk\\src\\CMakeFiles\\rby1-sdk_autogen.dir\\ParseCache.txt"
  "rby1-sdk\\src\\rby1-sdk_autogen"
  "rby1-sdk\\third-party\\CMakeFiles\\DynamixelSDK_autogen.dir\\AutogenUsed.txt"
  "rby1-sdk\\third-party\\CMakeFiles\\DynamixelSDK_autogen.dir\\ParseCache.txt"
  "rby1-sdk\\third-party\\DynamixelSDK_autogen"
  "rby1-sdk\\third-party\\osqp-eigen\\CMakeFiles\\OsqpEigen_autogen.dir\\AutogenUsed.txt"
  "rby1-sdk\\third-party\\osqp-eigen\\CMakeFiles\\OsqpEigen_autogen.dir\\ParseCache.txt"
  "rby1-sdk\\third-party\\osqp-eigen\\OsqpEigen_autogen"
  "rby1-sdk\\third-party\\osqp\\CMakeFiles\\OSQPLIB_autogen.dir\\AutogenUsed.txt"
  "rby1-sdk\\third-party\\osqp\\CMakeFiles\\OSQPLIB_autogen.dir\\ParseCache.txt"
  "rby1-sdk\\third-party\\osqp\\CMakeFiles\\osqpstatic_autogen.dir\\AutogenUsed.txt"
  "rby1-sdk\\third-party\\osqp\\CMakeFiles\\osqpstatic_autogen.dir\\ParseCache.txt"
  "rby1-sdk\\third-party\\osqp\\OSQPLIB_autogen"
  "rby1-sdk\\third-party\\osqp\\osqpstatic_autogen"
  )
endif()
