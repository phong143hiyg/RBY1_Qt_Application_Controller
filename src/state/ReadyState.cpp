#include "state/ReadyState.hpp"

#include "controller/RobotController.hpp"
#include "state/DrivingState.hpp"
#include "state/JointBusyState.hpp"
#include "state/PreparingState.hpp"

std::unique_ptr<RobotState> ReadyState::prepare(
    RobotController &controller)
{
    controller.stopVelocityInternal();

    auto preparing = std::make_unique<PreparingState>();

    if (!preparing->start(controller))
    {
        return nullptr;
    }

    return preparing;
}

std::unique_ptr<RobotState> ReadyState::startDrive(
    RobotController &controller,
    double linearX,
    double linearY,
    double angularZ)
{
    controller.startVelocityInternal(
        linearX,
        linearY,
        angularZ);

    return std::make_unique<DrivingState>();
}

std::unique_ptr<RobotState> ReadyState::nudgeJoint(
    RobotController &controller,
    const QString &groupName,
    int jointIndex,
    double delta,
    double minimumTime)
{
    controller.stopVelocityInternal();

    const bool sent =
        controller.sendJointNudgeInternal(
            groupName,
            jointIndex,
            delta,
            minimumTime);

    if (!sent)
    {
        return nullptr;
    }

    return std::make_unique<JointBusyState>(
        QStringLiteral("Joint nudge"));
}

std::unique_ptr<RobotState> ReadyState::sendPose(
    RobotController &controller,
    const QString &command,
    const QString &operationName,
    double minimumTime)
{
    controller.stopVelocityInternal();

    const bool sent =
        controller.sendPoseInternal(
            command,
            operationName,
            minimumTime);

    if (!sent)
    {
        return nullptr;
    }

    return std::make_unique<JointBusyState>(
        operationName);
}
