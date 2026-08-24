#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

class RobotClient;
class RobotState;

class RobotController final : public QObject
{
    Q_OBJECT

public:
    explicit RobotController(QObject *parent = nullptr);
    ~RobotController() override;

    void connectToBridge();
    void disconnectFromBridge();

    void ping();
    void requestStatus();

    void prepareRobot();

    void setPower(bool enabled);
    void setServo(bool enabled);
    void setStream(bool enabled);
    void cancelControl();

    void startDrive(
        double linearX,
        double linearY,
        double angularZ);

    void stopDrive();

    void refreshJoints();

    void nudgeJoint(
        const QString &groupName,
        int jointIndex,
        double delta,
        double minimumTime);

    void sendPose(
        const QString &command,
        const QString &operationName,
        double minimumTime);

    // API dùng bởi các State.
    bool sendSimpleInternal(
        const QString &command,
        const QString &operationName);

    bool sendSwitchInternal(
        const QString &command,
        bool enabled,
        const QString &operationName);

    void startVelocityInternal(
        double linearX,
        double linearY,
        double angularZ);

    void stopVelocityInternal();

    bool sendJointNudgeInternal(
        const QString &groupName,
        int jointIndex,
        double delta,
        double minimumTime);

    bool sendPoseInternal(
        const QString &command,
        const QString &operationName,
        double minimumTime);

    void scheduleJointRefresh();

    void rejectAction(const QString &reason);
    void appendStateLog(const QString &message);

signals:
    void stateChanged(
        const QString &stateName,
        bool connected,
        bool canDrive,
        bool canControlJoints,
        bool canChangeSystemConfiguration,
        bool busy);

    void systemConfigurationChanged(
        bool powerEnabled,
        bool servoEnabled,
        bool streamEnabled);

    void responseReceived(
        const QString &operationName,
        const QJsonObject &response);

    void logMessage(const QString &message);

    void jointStatusReceived(
        const QJsonObject &response);

private slots:
    void sendVelocityTick();

    void handleBridgeConnected();
    void handleBridgeDisconnected();

    void handleResponse(
        const QString &operationName,
        const QJsonObject &response);

private:
    void transitionTo(
        std::unique_ptr<RobotState> nextState);

    void applyTransition(
        std::unique_ptr<RobotState> nextState);

    void emitCurrentState();

    void updateStateFromStatus(
        const QJsonObject &response);

    void updateSystemConfigurationFromStatus(
        const QJsonObject &response);

    void emitSystemConfiguration();

    RobotClient *client_{nullptr};
    QTimer velocityTimer_;
    QTimer statusTimer_;

    std::unique_ptr<RobotState> state_;

    double linearX_{0.0};
    double linearY_{0.0};
    double angularZ_{0.0};

    bool statusRequestPending_{false};

    bool powerEnabled_{false};
    bool servoEnabled_{false};
    bool streamEnabled_{false};
};
