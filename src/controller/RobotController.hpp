#pragma once

#include "model/SystemStatus.hpp"
#include "network/RobotClient.hpp"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

class RobotState;

class RobotController final : public QObject
{
    Q_OBJECT

public:
    explicit RobotController(QObject *parent = nullptr);
    ~RobotController() override;

    void connectToRobot(
        const QString &host = QStringLiteral("127.0.0.1"),
        quint16 port = 55051,
        Rby1Model model = Rby1Model::M);
    void disconnectFromRobot();

    void ping();
    void requestStatus();

    void prepareRobot();

    void togglePower();
    void toggleServo();
    void toggleStream();

    // Explicit target setters are retained for API compatibility. They use
    // the same confirmed/pending pipeline as the UI toggle actions.
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

    void moveJointTo(const QString &groupName, int jointIndex,
                     double targetRadians, double minimumTime);

    void sendPose(
        const QString &command,
        const QString &operationName,
        double minimumTime);

    // API used by RobotState implementations.
    bool sendSimpleInternal(
        const QString &command,
        const QString &operationName);

    bool beginPreparationInternal();

    void startVelocityInternal(
        double linearX,
        double linearY,
        double angularZ);

    void stopVelocityInternal();

    quint64 sendJointNudgeInternal(
        const QString &groupName,
        int jointIndex,
        double delta,
        double minimumTime);

    quint64 requestJointSnapshotInternal();

    quint64 sendPoseInternal(
        const QString &command,
        const QString &operationName,
        double minimumTime);

    void scheduleJointRefresh();

    void rejectAction(const QString &reason);
    void appendStateLog(const QString &message);
    void reportJointMotionFailure(const QString &message);

    [[nodiscard]] SystemConfigurationView systemConfiguration() const;

signals:
    void jointMotionFailed(const QString &message);

    void stateChanged(
        const QString &stateName,
        bool connected,
        bool canDrive,
        bool canControlJoints,
        bool canChangeSystemConfiguration,
        bool busy);

    void systemConfigurationChanged(
        const SystemConfigurationView &configuration);

    void responseReceived(
        const QString &operationName,
        const QJsonObject &response);

    void logMessage(const QString &message);

    void jointStatusReceived(
        const QJsonObject &response);

private slots:
    void sendVelocityTick();
    void checkTimeoutsAndFreshness();

    void handleRobotConnected();
    void handleRobotDisconnected();

    void handleResponse(
        quint64 requestId,
        const QString &operationName,
        const QJsonObject &response);

    void handleRequestTimeout(
        quint64 requestId,
        const QString &operationName);

private:
    struct ComponentRuntime
    {
        ComponentState confirmed{ComponentState::Unknown};
        ComponentState remoteState{ComponentState::Unknown};
        QString source;
        bool legacy{false};

        bool localPending{false};
        bool targetEnabled{false};
        quint64 commandRequestId{0};
        bool commandTimedOut{false};
        qint64 confirmationDeadlineMs{0};
    };

    void transitionTo(std::unique_ptr<RobotState> nextState);
    void applyTransition(std::unique_ptr<RobotState> nextState);
    void emitCurrentState();

    void updateStateFromStatus(const QJsonObject &response);
    void applySystemStatus(
        quint64 requestId,
        const QJsonObject &response);

    void emitSystemConfiguration();
    void markAllComponentsUnknown(bool clearPending);

    void toggleComponent(RobotComponent component);
    bool requestComponentTarget(
        RobotComponent component,
        bool enabled,
        bool preparationStep = false);

    void finishComponentCommand(
        quint64 requestId,
        const QJsonObject &response);

    void applyParsedComponent(
        RobotComponent component,
        const ComponentStatus &status);

    void continuePreparation();
    void abortPreparation(const QString &reason);
    void forceStatusRefresh();
    void extendStatusFreshnessGrace(int durationMs);

    [[nodiscard]] ComponentRuntime &runtime(RobotComponent component);
    [[nodiscard]] const ComponentRuntime &runtime(
        RobotComponent component) const;
    [[nodiscard]] ComponentViewState makeView(
        const ComponentRuntime &component) const;
    [[nodiscard]] bool anyComponentPending() const;
    [[nodiscard]] bool componentIsConfirmedOn(
        RobotComponent component) const;

    RobotClient *client_{nullptr};
    Rby1Model activeModel_{Rby1Model::M};
    QTimer velocityTimer_;
    QTimer statusTimer_;
    QTimer jointStatusTimer_;
    QTimer healthTimer_;
    QElapsedTimer monotonicClock_;

    std::unique_ptr<RobotState> state_;

    double linearX_{0.0};
    double linearY_{0.0};
    double angularZ_{0.0};

    quint64 statusRequestPendingId_{0};
    quint64 jointStatusRequestPendingId_{0};
    quint64 latestAppliedJointStatusRequestId_{0};
    quint64 latestAppliedStatusRequestId_{0};
    bool immediateStatusRefreshQueued_{false};
    qint64 lastValidStatusMs_{-1};
    qint64 statusFreshnessGraceDeadlineMs_{0};
    bool statusMarkedStale_{true};

    bool preparationRequested_{false};

    ComponentRuntime power_;
    ComponentRuntime servo_;
    ComponentRuntime stream_;

    static constexpr int kCommandTimeoutMs = 3000;
    static constexpr int kConfirmationTimeoutMs = 5000;
    static constexpr int kStatusStaleMs = 2500;
    static constexpr int kPostMotionStatusGraceMs = 2500;
};
