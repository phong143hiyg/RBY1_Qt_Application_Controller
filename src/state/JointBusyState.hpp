#pragma once

#include "state/RobotState.hpp"

class JointBusyState final : public RobotState
{
public:
    JointBusyState(
        QString pendingOperation,
        quint64 pendingRequestId);

    JointBusyState(
        QString pendingOperation,
        quint64 pendingRequestId,
        QString groupName,
        int jointIndex,
        double remainingDelta,
        double segmentMinimumTime);

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
        quint64 requestId,
        const QString &operationName,
        const QJsonObject &response) override;

    std::unique_ptr<RobotState> onRequestTimeout(
        RobotController &controller,
        quint64 requestId,
        const QString &operationName) override;

private:
    QString pendingOperation_;
    quint64 pendingRequestId_{0};
    QString groupName_;
    int jointIndex_{-1};
    double remainingDelta_{0.0};
    double segmentMinimumTime_{0.0};
};
