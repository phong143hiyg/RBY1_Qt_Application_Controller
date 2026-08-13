#include "state/PreparingState.hpp"

#include "controller/RobotController.hpp"
#include "state/ConnectedState.hpp"
#include "state/ReadyState.hpp"

std::unique_ptr<RobotState> PreparingState::onResponse(
    RobotController &controller,
    const QString &operationName,
    const QJsonObject &response)
{
    if (operationName != QStringLiteral("Chuẩn bị robot"))
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
                "Prepare thành công. Robot chuyển sang Ready."));

        return std::make_unique<ReadyState>();
    }

    controller.appendStateLog(
        QStringLiteral(
            "Prepare thất bại. Robot quay về Connected."));

    return std::make_unique<ConnectedState>();
}
