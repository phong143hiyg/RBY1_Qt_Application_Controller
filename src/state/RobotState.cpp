#include "state/RobotState.hpp"

#include "controller/RobotController.hpp"

std::unique_ptr<RobotState> RobotState::prepare(
    RobotController &controller)
{
    controller.rejectAction(
        QStringLiteral(
            "Không thể Prepare trong trạng thái %1.")
            .arg(name()));

    return nullptr;
}

std::unique_ptr<RobotState> RobotState::startDrive(
    RobotController &controller,
    double,
    double,
    double)
{
    controller.rejectAction(
        QStringLiteral(
            "Không thể điều khiển đế trong trạng thái %1.")
            .arg(name()));

    return nullptr;
}

std::unique_ptr<RobotState> RobotState::stopDrive(
    RobotController &controller)
{
    controller.stopVelocityInternal();
    return nullptr;
}

std::unique_ptr<RobotState> RobotState::nudgeJoint(
    RobotController &controller,
    const QString &,
    int,
    double,
    double)
{
    controller.rejectAction(
        QStringLiteral(
            "Không thể điều khiển khớp trong trạng thái %1.")
            .arg(name()));

    return nullptr;
}

std::unique_ptr<RobotState> RobotState::sendPose(
    RobotController &controller,
    const QString &,
    const QString &,
    double)
{
    controller.rejectAction(
        QStringLiteral(
            "Không thể gửi pose trong trạng thái %1.")
            .arg(name()));

    return nullptr;
}

std::unique_ptr<RobotState> RobotState::onResponse(
    RobotController &,
    const QString &,
    const QJsonObject &)
{
    return nullptr;
}
