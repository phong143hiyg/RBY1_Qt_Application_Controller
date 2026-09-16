#include "state/JointBusyState.hpp"

#include "controller/RobotController.hpp"
#include "state/ReadyState.hpp"

namespace
{
constexpr double kMaxNudgeDeltaRadians = 0.20;
}

JointBusyState::JointBusyState(
    QString pendingOperation,
    quint64 pendingRequestId)
    : pendingOperation_(
          std::move(pendingOperation)),
      pendingRequestId_(pendingRequestId)
{
}

JointBusyState::JointBusyState(
    QString pendingOperation,
    quint64 pendingRequestId,
    QString groupName,
    int jointIndex,
    double remainingDelta,
    double segmentMinimumTime)
    : pendingOperation_(
          std::move(pendingOperation)),
      pendingRequestId_(pendingRequestId),
      groupName_(
          std::move(groupName)),
      jointIndex_(jointIndex),
      remainingDelta_(remainingDelta),
      segmentMinimumTime_(segmentMinimumTime)
{
}

std::unique_ptr<RobotState> JointBusyState::onResponse(
    RobotController &controller,
    quint64 requestId,
    const QString &operationName,
    const QJsonObject &response)
{
    if (requestId != pendingRequestId_
        || operationName != pendingOperation_)
    {
        return nullptr;
    }

    const bool success =
        response.value(
            QStringLiteral("success")).toBool(false);

    if (!success)
    {
        if (pendingOperation_ == QStringLiteral("Joint nudge"))
        {
            QString reason = response.value(QStringLiteral("message")).toString();
            if (reason.isEmpty())
            {
                reason = response.value(QStringLiteral("error")).toString();
            }
            controller.reportJointMotionFailure(reason.isEmpty()
                ? QStringLiteral("App Bridge từ chối lệnh thay đổi góc khớp.") : reason);
        }
        controller.appendStateLog(
            QStringLiteral("%1 failed; returning to Ready.")
                .arg(pendingOperation_));
        return std::make_unique<ReadyState>();
    }

    if (remainingDelta_ > 1e-9 || remainingDelta_ < -1e-9)
    {
        const double nextDelta =
            remainingDelta_ > kMaxNudgeDeltaRadians
                ? kMaxNudgeDeltaRadians
                : remainingDelta_ < -kMaxNudgeDeltaRadians
                    ? -kMaxNudgeDeltaRadians
                    : remainingDelta_;

        const quint64 nextRequestId =
            controller.sendJointNudgeInternal(
                groupName_,
                jointIndex_,
                nextDelta,
                segmentMinimumTime_);

        if (nextRequestId != 0)
        {
            remainingDelta_ -= nextDelta;
            pendingRequestId_ = nextRequestId;
            return nullptr;
        }

        controller.appendStateLog(
            QStringLiteral("Could not send the next joint nudge; returning to Ready."));
        controller.reportJointMotionFailure(QStringLiteral("Không gửi được đoạn chuyển động khớp tiếp theo."));
        return std::make_unique<ReadyState>();
    }

    controller.appendStateLog(
        QStringLiteral("%1 completed; returning to Ready.")
            .arg(pendingOperation_));
    controller.scheduleJointRefresh();
    return std::make_unique<ReadyState>();
}

std::unique_ptr<RobotState> JointBusyState::onRequestTimeout(
    RobotController &controller,
    quint64 requestId,
    const QString &operationName)
{
    if (requestId != pendingRequestId_
        || operationName != pendingOperation_)
    {
        return nullptr;
    }

    controller.appendStateLog(
        QStringLiteral(
            "%1 timed out; releasing JointBusy and refreshing robot state.")
            .arg(pendingOperation_));
    if (pendingOperation_ == QStringLiteral("Joint nudge"))
    {
        controller.reportJointMotionFailure(QStringLiteral(
            "Hết thời gian chờ App Bridge xác nhận lệnh thay đổi góc khớp. Đang đọc lại trạng thái robot."));
    }
    controller.scheduleJointRefresh();
    return std::make_unique<ReadyState>();
}
