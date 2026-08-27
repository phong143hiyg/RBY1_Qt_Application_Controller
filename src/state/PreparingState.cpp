#include "state/PreparingState.hpp"

#include "controller/RobotController.hpp"
#include "state/ConnectedState.hpp"
#include "state/ReadyState.hpp"

bool PreparingState::start(RobotController &controller)
{
    step_ = Step::PowerOn;

    return controller.sendSwitchInternal(
        QStringLiteral("power"),
        true,
        QStringLiteral("Power ON"));
}

std::unique_ptr<RobotState> PreparingState::onResponse(
    RobotController &controller,
    const QString &operationName,
    const QJsonObject &response)
{
    const QString expectedOperation =
        step_ == Step::PowerOn
            ? QStringLiteral("Power ON")
            : step_ == Step::ServoOn
                ? QStringLiteral("Servo ON")
                : QStringLiteral("Stream ON");

    // Ignore status or stale responses; only the current step may advance.
    if (operationName != expectedOperation)
    {
        return nullptr;
    }

    if (!response.value(QStringLiteral("success")).toBool(false))
    {
        controller.appendStateLog(
            QStringLiteral("Preparation failed at %1; returning to Connected.")
                .arg(operationName));
        return std::make_unique<ConnectedState>();
    }

    switch (step_)
    {
    case Step::PowerOn:
        step_ = Step::ServoOn;
        if (controller.sendSwitchInternal(
                QStringLiteral("servo"), true, QStringLiteral("Servo ON")))
        {
            return nullptr;
        }
        break;

    case Step::ServoOn:
        step_ = Step::StreamOn;
        if (controller.sendSwitchInternal(
                QStringLiteral("stream"), true, QStringLiteral("Stream ON")))
        {
            return nullptr;
        }
        break;

    case Step::StreamOn:
        controller.appendStateLog(
            QStringLiteral("Preparation succeeded; robot is Ready."));
        return std::make_unique<ReadyState>();
    }

    controller.appendStateLog(
        QStringLiteral("Could not send the next preparation command; returning to Connected."));
    return std::make_unique<ConnectedState>();
}
