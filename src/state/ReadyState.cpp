#include "state/ReadyState.hpp"

#include "controller/RobotController.hpp"
#include "state/DrivingState.hpp"
#include "state/JointBusyState.hpp"
#include "state/PreparingState.hpp"

#include <QtMath>

namespace
{
constexpr double kMaxNudgeDeltaRadians = 0.20;
constexpr double kMinimumSegmentTimeSeconds = 0.20;
}

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

    const int segmentCount = qMax(
        1,
        qCeil(qAbs(delta) / kMaxNudgeDeltaRadians));
    const double segmentMinimumTime = qMax(
        kMinimumSegmentTimeSeconds,
        qMax(0.0, minimumTime)
            / static_cast<double>(segmentCount));

    const double firstDelta =
        delta > kMaxNudgeDeltaRadians
            ? kMaxNudgeDeltaRadians
            : delta < -kMaxNudgeDeltaRadians
                ? -kMaxNudgeDeltaRadians
                : delta;

    const quint64 requestId =
        controller.sendJointNudgeInternal(
            groupName,
            jointIndex,
            firstDelta,
            segmentMinimumTime);

    if (requestId == 0)
    {
        controller.reportJointMotionFailure(QStringLiteral("Không gửi được lệnh thay đổi góc khớp tới Robot SDK."));
        return nullptr;
    }

    const double remainingDelta =
        delta - firstDelta;

    if (remainingDelta > 1e-9 || remainingDelta < -1e-9)
    {
        return std::make_unique<JointBusyState>(
            QStringLiteral("Joint nudge"),
            requestId,
            groupName,
            jointIndex,
            remainingDelta,
            segmentMinimumTime);
    }

    return std::make_unique<JointBusyState>(
        QStringLiteral("Joint nudge"),
        requestId);
}

std::unique_ptr<RobotState> ReadyState::sendPose(
    RobotController &controller,
    const QString &command,
    const QString &operationName,
    double minimumTime)
{
    controller.stopVelocityInternal();

    const quint64 requestId =
        controller.sendPoseInternal(
            command,
            operationName,
            minimumTime);

    if (requestId == 0)
    {
        return nullptr;
    }

    return std::make_unique<JointBusyState>(
        operationName,
        requestId);
}
