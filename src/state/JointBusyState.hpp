#pragma once

#include "state/RobotState.hpp"

class JointBusyState final : public RobotState
{
public:
    JointBusyState(quint64 snapshotRequestId, QString groupName, int jointIndex,
                   double targetRadians);

    JointBusyState(
        QString pendingOperation,
        quint64 pendingRequestId);

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
    bool absoluteTarget_{false};
    double targetRadians_{0.0};
    int correctionsRemaining_{8};
};
