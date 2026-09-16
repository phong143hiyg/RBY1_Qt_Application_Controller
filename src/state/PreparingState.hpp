#pragma once

#include "state/RobotState.hpp"

class PreparingState final : public RobotState
{
public:
    bool start(RobotController &controller);

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Preparing");
    }

    [[nodiscard]] bool isBusy() const override
    {
        return true;
    }

    std::unique_ptr<RobotState> onResponse(
        RobotController &controller,
        quint64 requestId,
        const QString &operationName,
        const QJsonObject &response) override;

};
