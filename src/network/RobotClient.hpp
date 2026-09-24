#pragma once

#include "model/SystemStatus.hpp"

#include <QJsonObject>
#include <QObject>
#include <QString>

enum class Rby1Model
{
    A,
    M
};

// In-process interface shared by the controller and the manufacturer's SDK adapter.
// Robot commands are never serialized to the planning NDJSON service.
class RobotClient : public QObject
{
    Q_OBJECT

public:
    explicit RobotClient(QObject *parent = nullptr);
    ~RobotClient() override;

    virtual void connectToRobot(const QString &host, quint16 port, Rby1Model model) = 0;
    virtual void disconnectFromRobot() = 0;
    [[nodiscard]] virtual bool isConnected() const = 0;

    virtual quint64 readStatus(int timeoutMs) = 0;
    virtual quint64 readJoints(int timeoutMs) = 0;
    virtual quint64 setComponent(RobotComponent component, bool enabled,
                                 const QString &operationName, int timeoutMs) = 0;
    virtual quint64 moveJointRelative(const QString &groupName, int jointIndex,
                                      double delta, double minimumTime, int timeoutMs) = 0;
    virtual quint64 executePose(const QString &pose, const QString &operationName,
                                double minimumTime, int timeoutMs) = 0;
    virtual quint64 executeSimple(const QString &action, const QString &operationName,
                                  int timeoutMs) = 0;
    virtual quint64 setVelocity(double x, double y, double angularZ) = 0;

signals:
    void robotConnected();
    void robotDisconnected();
    void responseReceived(quint64 requestId, const QString &operationName,
                          const QJsonObject &response);
    void requestTimedOut(quint64 requestId, const QString &operationName);
    void clientError(const QString &message);
};
