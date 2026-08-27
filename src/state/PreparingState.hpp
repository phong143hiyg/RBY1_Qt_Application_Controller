#pragma once

#include "state/RobotState.hpp"

class PreparingState final : public RobotState
{
public:
    enum class Step
    {
        PowerOn,
        ServoOn,
        StreamOn
    };

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
        const QString &operationName,
        const QJsonObject &response) override;

private:
    Step step_{Step::PowerOn};
};
