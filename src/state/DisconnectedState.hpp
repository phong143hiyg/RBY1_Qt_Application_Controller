#pragma once

#include "state/RobotState.hpp"

class DisconnectedState final : public RobotState
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Disconnected");
    }

    [[nodiscard]] bool isConnected() const override
    {
        return false;
    }
};
