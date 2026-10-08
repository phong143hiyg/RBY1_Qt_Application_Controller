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

// Qt-facing, in-process abstraction for the Rainbow Robotics client.  All
// operations are asynchronous: implementations emit responseReceived or
// requestTimedOut and must never block the GUI thread.
class IRby1Client : public QObject
{
    Q_OBJECT

public:
    explicit IRby1Client(QObject *parent = nullptr);
    ~IRby1Client() override;

    virtual void connectToRobot(const QString &host, quint16 port, Rby1Model model) = 0;
    virtual void disconnectFromRobot() = 0;
    [[nodiscard]] virtual bool isConnected() const = 0;

    virtual quint64 readStatus(int timeoutMs) = 0;
    virtual quint64 readJoints(int timeoutMs) = 0;
    virtual quint64 setComponent(RobotComponent component, bool enabled,
                                 const QString &operationName, int timeoutMs) = 0;
    virtual quint64 moveJointTo(const QString &groupName, int jointIndex,
                                double targetRadians, int timeoutMs) = 0;
    virtual quint64 executePose(const QString &pose, const QString &operationName,
                                int timeoutMs) = 0;
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
