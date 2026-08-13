#pragma once

#include "state/RobotState.hpp"

class DrivingState final : public RobotState
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Driving");
    }

    [[nodiscard]] bool canDrive() const override
    {
        return true;
    }

    std::unique_ptr<RobotState> startDrive(
        RobotController &controller,
        double linearX,
        double linearY,
        double angularZ) override;

    std::unique_ptr<RobotState> stopDrive(
        RobotController &controller) override;
};
