cmake_minimum_required(VERSION 3.21)

project(RBY1DesktopQt
    VERSION 0.4.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Network
)

qt_standard_project_setup()

qt_add_executable(RBY1DesktopQt
    src/main.cpp

    src/ui/MainWindow.hpp
    src/ui/MainWindow.cpp

    src/ui/ToggleSwitch.hpp
    src/ui/ToggleSwitch.cpp

    src/network/RobotClient.hpp
    src/network/RobotClient.cpp

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
)

target_include_directories(RBY1DesktopQt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(RBY1DesktopQt PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

set_target_properties(RBY1DesktopQt PROPERTIES
    WIN32_EXECUTABLE TRUE
)
