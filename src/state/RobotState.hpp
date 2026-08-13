#pragma once

#include <QJsonObject>
#include <QString>

#include <memory>

class RobotController;

class RobotState
{
public:
    virtual ~RobotState() = default;

    [[nodiscard]] virtual QString name() const = 0;

    [[nodiscard]] virtual bool isConnected() const
    {
        return true;
    }

    [[nodiscard]] virtual bool canDrive() const
    {
        return false;
    }

    [[nodiscard]] virtual bool canControlJoints() const
    {
        return false;
    }

    [[nodiscard]] virtual bool isBusy() const
    {
        return false;
    }

    virtual std::unique_ptr<RobotState> prepare(
        RobotController &controller);

    virtual std::unique_ptr<RobotState> startDrive(
        RobotController &controller,
        double linearX,
        double linearY,
        double angularZ);

    virtual std::unique_ptr<RobotState> stopDrive(
        RobotController &controller);

    virtual std::unique_ptr<RobotState> nudgeJoint(
        RobotController &controller,
        const QString &groupName,
        int jointIndex,
        double delta,
        double minimumTime);

    virtual std::unique_ptr<RobotState> sendPose(
        RobotController &controller,
        const QString &command,
        const QString &operationName,
        double minimumTime);

    virtual std::unique_ptr<RobotState> onResponse(
        RobotController &controller,
        const QString &operationName,
        const QJsonObject &response);
};
