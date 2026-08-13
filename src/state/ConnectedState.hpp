#pragma once

#include "state/RobotState.hpp"

class ConnectedState final : public RobotState
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Connected");
    }

    std::unique_ptr<RobotState> prepare(
        RobotController &controller) override;
};
