#include "state/ReadyState.hpp"

#include "controller/RobotController.hpp"
#include "state/DrivingState.hpp"
#include "state/JointBusyState.hpp"
#include "state/PreparingState.hpp"

#include <QtMath>

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
    double delta)
{
    controller.stopVelocityInternal();

    const quint64 requestId =
        controller.sendJointNudgeInternal(
            groupName,
            jointIndex,
            delta);

    if (requestId == 0)
    {
        controller.reportJointMotionFailure(QStringLiteral("Không gửi được lệnh thay đổi góc khớp tới Robot SDK."));
        return nullptr;
    }

    return std::make_unique<JointBusyState>(
        QStringLiteral("Joint nudge"),
        requestId);
}

std::unique_ptr<RobotState> ReadyState::sendPose(
    RobotController &controller,
    const QString &command,
    const QString &operationName)
{
    controller.stopVelocityInternal();

    const quint64 requestId =
        controller.sendPoseInternal(
            command,
            operationName);

    if (requestId == 0)
    {
        return nullptr;
    }

    return std::make_unique<JointBusyState>(
        operationName,
        requestId);
}
