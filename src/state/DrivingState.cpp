#include "state/DrivingState.hpp"

#include "controller/RobotController.hpp"
#include "state/ReadyState.hpp"

std::unique_ptr<RobotState> DrivingState::startDrive(
    RobotController &controller,
    double linearX,
    double linearY,
    double angularZ)
{
    controller.startVelocityInternal(
        linearX,
        linearY,
        angularZ);

    return nullptr;
}

std::unique_ptr<RobotState> DrivingState::stopDrive(
    RobotController &controller)
{
    controller.stopVelocityInternal();

    return std::make_unique<ReadyState>();
}
