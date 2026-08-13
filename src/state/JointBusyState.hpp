#pragma once

#include "state/RobotState.hpp"

class JointBusyState final : public RobotState
{
public:
    explicit JointBusyState(QString pendingOperation);

    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("JointBusy");
    }

    [[nodiscard]] bool isBusy() const override
    {
        return true;
    }

    std::unique_ptr<RobotState> onResponse(
        RobotController &controller,
        const QString &operationName,
        const QJsonObject &response) override;

private:
    QString pendingOperation_;
};
