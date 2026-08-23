#pragma once

#include "state/RobotState.hpp"

class ReadyState final : public RobotState
{
public:
    [[nodiscard]] QString name() const override
    {
        return QStringLiteral("Ready");
    }

    [[nodiscard]] bool canDrive() const override
    {
        return true;
    }

    [[nodiscard]] bool canControlJoints() const override
    {
        return true;
    }

    [[nodiscard]] bool canChangeSystemConfiguration() const override
    {
        return true;
    }

    std::unique_ptr<RobotState> prepare(
        RobotController &controller) override;

    std::unique_ptr<RobotState> startDrive(
        RobotController &controller,
        double linearX,
        double linearY,
        double angularZ) override;

    std::unique_ptr<RobotState> nudgeJoint(
        RobotController &controller,
        const QString &groupName,
        int jointIndex,
        double delta,
        double minimumTime) override;

    std::unique_ptr<RobotState> sendPose(
        RobotController &controller,
        const QString &command,
        const QString &operationName,
        double minimumTime) override;
};
