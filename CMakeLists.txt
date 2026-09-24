cmake_minimum_required(VERSION 3.21)

project(RBY1DesktopQt
    VERSION 0.4.1
    LANGUAGES CXX
)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Network
)

qt_standard_project_setup()

# Build the SDK shipped with this repository instead of resolving a separately
# installed rby1-sdk package. The source ZIP omits its Git submodule contents.
set(RBY1_SDK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rby1-sdk-main")
if(NOT EXISTS "${RBY1_SDK_SOURCE_DIR}/third-party/DynamixelSDK/c++/src/dynamixel_sdk/packet_handler.cpp")
    message(FATAL_ERROR "Missing rby1-sdk submodule DynamixelSDK; see README.md")
endif()
foreach(SDK_SUBMODULE IN ITEMS osqp osqp-eigen)
    if(NOT EXISTS "${RBY1_SDK_SOURCE_DIR}/third-party/${SDK_SUBMODULE}/CMakeLists.txt")
        message(FATAL_ERROR "Missing rby1-sdk submodule ${SDK_SUBMODULE}; see README.md")
    endif()
endforeach()
if(NOT EXISTS "${RBY1_SDK_SOURCE_DIR}/third-party/qdldl/CMakeLists.txt")
    message(FATAL_ERROR "Missing OSQP dependency QDLDL; see README.md")
endif()
# OSQP normally downloads QDLDL during configure. Use the checked-in tag.
set(FETCHCONTENT_SOURCE_DIR_QDLDL "${RBY1_SDK_SOURCE_DIR}/third-party/qdldl"
    CACHE PATH "Local QDLDL source for the vendored rby1-sdk" FORCE)
set(BUILD_LIB_STATIC ON CACHE BOOL "Link the vendored rby1-sdk statically")
add_subdirectory("${RBY1_SDK_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rby1-sdk" EXCLUDE_FROM_ALL)
add_library(rby1-sdk::rby1-sdk ALIAS rby1-sdk)

set(PLANNING_SOURCES
    src/planning/PlanningClient.hpp src/planning/PlanningClient.cpp
    src/planning/PlanningInput.hpp src/planning/PlanningInput.cpp
    src/ui/PlanningPanel.hpp src/ui/PlanningPanel.cpp
)

qt_add_executable(RBY1DesktopQt
    ${PLANNING_SOURCES}
    src/main.cpp

    src/ui/MainWindow.hpp
    src/ui/MainWindow.cpp

    src/ui/JointAngleEdit.hpp
    src/ui/JointAngleEdit.cpp

    src/ui/ToggleSwitch.hpp
    src/ui/ToggleSwitch.cpp

    src/model/SystemStatus.hpp
    src/model/SystemStatus.cpp

    src/network/RobotClient.hpp
    src/network/RobotClient.cpp
    src/network/NdjsonParser.hpp
    src/network/NdjsonParser.cpp

    src/controller/RobotController.hpp
    src/controller/RobotController.cpp

    src/state/RobotState.hpp
    src/state/RobotState.cpp

    src/state/DisconnectedState.hpp
    src/state/DisconnectedState.cpp

    src/state/ConnectedState.hpp
    src/state/ConnectedState.cpp

    src/state/PreparingState.hpp
    src/state/PreparingState.cpp

    src/state/ReadyState.hpp
    src/state/ReadyState.cpp

    src/state/DrivingState.hpp
    src/state/DrivingState.cpp

    src/state/JointBusyState.hpp
    src/state/JointBusyState.cpp
    src/sdk/SdkRobotClient.hpp
    src/sdk/SdkRobotClient.cpp
)

target_include_directories(RBY1DesktopQt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(RBY1DesktopQt PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    rby1-sdk::rby1-sdk
)

set_target_properties(RBY1DesktopQt PROPERTIES
    WIN32_EXECUTABLE TRUE
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(RBY1SdkTests tests/TestSdkRobotClient.cpp
        src/sdk/SdkRobotClient.hpp src/sdk/SdkRobotClient.cpp
        src/network/RobotClient.hpp src/network/RobotClient.cpp)
    target_include_directories(RBY1SdkTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(RBY1SdkTests PRIVATE Qt6::Core Qt6::Network Qt6::Test rby1-sdk::rby1-sdk)
    set_target_properties(RBY1SdkTests PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME RBY1SdkTests COMMAND RBY1SdkTests -o -,txt -o sdk-results.xml,junitxml)

    qt_add_executable(RBY1PlanningTests tests/TestPlanning.cpp ${PLANNING_SOURCES}
        src/network/NdjsonParser.hpp src/network/NdjsonParser.cpp)
    target_include_directories(RBY1PlanningTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(RBY1PlanningTests PRIVATE
        PLANNING_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/planning_protocol/fixtures/contract-v1.json"
        PLANNING_MOCK_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tools/mock_planning_server.py")
    target_link_libraries(RBY1PlanningTests PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::Test)
    set_target_properties(RBY1PlanningTests PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME RBY1PlanningTests COMMAND RBY1PlanningTests -o -,txt -o planning-results.xml,junitxml)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME PlanningMockTests COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mock_planning.py)
    endif()
endif()
