#include "state/JointBusyState.hpp"

#include "controller/RobotController.hpp"
#include "state/ConnectedState.hpp"
#include "state/ReadyState.hpp"

JointBusyState::JointBusyState(
    QString pendingOperation,
    quint64 pendingRequestId)
    : pendingOperation_(std::move(pendingOperation)),
      pendingRequestId_(pendingRequestId)
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

    if (!response.value(QStringLiteral("success")).toBool(false))
    {
        QString reason = response.value(QStringLiteral("message")).toString();
        if (reason.isEmpty())
        {
            reason = response.value(QStringLiteral("error")).toString();
        }
        if (pendingOperation_ == QStringLiteral("Joint move"))
        {
            controller.reportJointMotionFailure(reason.isEmpty()
                ? QStringLiteral("Robot SDK từ chối lệnh vị trí khớp tuyệt đối.")
                : reason);
        }
        controller.appendStateLog(
            QStringLiteral(
                "%1 failed; waiting for canonical robot status. "
                "The SDK connection remains active.")
                .arg(pendingOperation_));
        controller.scheduleJointRefresh();
        controller.requestStatus();
        return std::make_unique<ConnectedState>();
    }

    controller.appendStateLog(
        QStringLiteral("%1 completed; returning to Ready.")
            .arg(pendingOperation_));
    controller.scheduleJointRefresh();
    controller.requestStatus();
    return std::make_unique<ConnectedState>();
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
    if (pendingOperation_ == QStringLiteral("Joint move"))
    {
        controller.reportJointMotionFailure(QStringLiteral(
            "Hết thời gian chờ SDK xác nhận lệnh vị trí khớp. Đang đọc lại trạng thái robot."));
    }
    controller.scheduleJointRefresh();
    return std::make_unique<ReadyState>();
}
