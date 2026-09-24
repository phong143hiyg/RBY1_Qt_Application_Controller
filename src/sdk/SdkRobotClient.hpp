#pragma once

#include "network/RobotClient.hpp"

#include <functional>
#include <memory>

// Adapter from the manufacturer SDK to the controller's in-process view model.
// QJsonObject is never sent over a robot-command socket.
class SdkRobotClient final : public RobotClient
{
public:
    explicit SdkRobotClient(QObject *parent = nullptr);
    ~SdkRobotClient() override;

    void connectToRobot(const QString &host, quint16 port, Rby1Model model) override;
    void disconnectFromRobot() override;
    bool isConnected() const override;
    quint64 readStatus(int timeoutMs) override;
    quint64 readJoints(int timeoutMs) override;
    quint64 setComponent(RobotComponent component, bool enabled,
                         const QString &operationName, int timeoutMs) override;
    quint64 moveJointRelative(const QString &groupName, int jointIndex,
                              double delta, double minimumTime, int timeoutMs) override;
    quint64 executePose(const QString &pose, const QString &operationName,
                        double minimumTime, int timeoutMs) override;
    quint64 executeSimple(const QString &action, const QString &operationName,
                          int timeoutMs) override;
    quint64 setVelocity(double x, double y, double angularZ) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
