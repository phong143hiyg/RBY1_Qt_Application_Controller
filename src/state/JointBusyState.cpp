#include "state/JointBusyState.hpp"

#include "controller/RobotController.hpp"
#include "state/ReadyState.hpp"

JointBusyState::JointBusyState(
    QString pendingOperation)
    : pendingOperation_(
          std::move(pendingOperation))
{
}

std::unique_ptr<RobotState> JointBusyState::onResponse(
    RobotController &controller,
    const QString &operationName,
    const QJsonObject &response)
{
    if (operationName != pendingOperation_)
    {
        return nullptr;
    }

    const bool success =
        response.value(
            QStringLiteral("success")).toBool(false);

    if (success)
    {
        controller.appendStateLog(
            QStringLiteral(
                "%1 hoàn thành. Quay về Ready.")
                .arg(pendingOperation_));

        controller.scheduleJointRefresh();
    }
    else
    {
        controller.appendStateLog(
            QStringLiteral(
                "%1 thất bại. Quay về Ready.")
                .arg(pendingOperation_));
    }

    return std::make_unique<ReadyState>();
}
