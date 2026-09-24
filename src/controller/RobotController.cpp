#include "controller/RobotController.hpp"

#include "network/RobotClient.hpp"
#include "sdk/SdkRobotClient.hpp"

#include "state/ConnectedState.hpp"
#include "state/DisconnectedState.hpp"
#include "state/JointBusyState.hpp"
#include "state/ReadyState.hpp"
#include "state/RobotState.hpp"

#include <QJsonValue>
#include <QTimer>
#include <QtMath>

namespace
{
QString componentName(RobotComponent component)
{
    switch (component)
    {
    case RobotComponent::Power:
        return QStringLiteral("Power");
    case RobotComponent::Servo:
        return QStringLiteral("Servo");
    case RobotComponent::Stream:
        return QStringLiteral("Control Manager");
    }

    return QStringLiteral("Component");
}

QString responseError(const QJsonObject &response)
{
    const QJsonValue message = response.value(QStringLiteral("message"));
    if (message.isString() && !message.toString().isEmpty())
    {
        return message.toString();
    }

    const QJsonValue error = response.value(QStringLiteral("error"));
    if (error.isString() && !error.toString().isEmpty())
    {
        return error.toString();
    }

    return QStringLiteral("Robot SDK rejected the command without an error message");
}
}

RobotController::RobotController(QObject *parent)
    : QObject(parent),
      client_(new SdkRobotClient(this)),
      state_(std::make_unique<DisconnectedState>())
{
    qRegisterMetaType<SystemConfigurationView>();

    monotonicClock_.start();
    velocityTimer_.setInterval(100);
    statusTimer_.setInterval(500);
    jointStatusTimer_.setInterval(500);
    healthTimer_.setInterval(100);

    connect(
        &velocityTimer_,
        &QTimer::timeout,
        this,
        &RobotController::sendVelocityTick);

    connect(
        &statusTimer_,
        &QTimer::timeout,
        this,
        &RobotController::requestStatus);

    connect(
        &jointStatusTimer_,
        &QTimer::timeout,
        this,
        &RobotController::refreshJoints);

    connect(
        &healthTimer_,
        &QTimer::timeout,
        this,
        &RobotController::checkTimeoutsAndFreshness);

    connect(
        client_,
        &RobotClient::robotConnected,
        this,
        &RobotController::handleRobotConnected);

    connect(
        client_,
        &RobotClient::robotDisconnected,
        this,
        &RobotController::handleRobotDisconnected);

    connect(
        client_,
        &RobotClient::responseReceived,
        this,
        &RobotController::handleResponse);

    connect(
        client_,
        &RobotClient::requestTimedOut,
        this,
        &RobotController::handleRequestTimeout);

    connect(
        client_,
        &RobotClient::clientError,
        this,
        [this](const QString &message)
        {
            emit logMessage(
                QStringLiteral("Robot connection error: %1").arg(message));
        });
}

RobotController::~RobotController() = default;

void RobotController::connectToRobot(
    const QString &host,
    quint16 port,
    Rby1Model model)
{
    if (client_->isConnected())
    {
        return;
    }

    activeModel_ = model;
    const QString modelName = model == Rby1Model::M ? QStringLiteral("M") : QStringLiteral("A");
    emit logMessage(QStringLiteral("Connecting to RBY1-%1 using SDK at %2:%3...")
                        .arg(modelName, host).arg(port));
    client_->connectToRobot(host, port, model);
}

void RobotController::disconnectFromRobot()
{
    stopVelocityInternal();
    client_->disconnectFromRobot();
}

void RobotController::ping()
{
    sendSimpleInternal(QStringLiteral("ping"), QStringLiteral("Ping"));
}

void RobotController::requestStatus()
{
    if (!client_->isConnected())
    {
        return;
    }

    if (statusRequestPendingId_ != 0)
    {
        return;
    }

    statusRequestPendingId_ = client_->readStatus(kCommandTimeoutMs);
}

void RobotController::forceStatusRefresh()
{
    if (!client_->isConnected())
    {
        return;
    }

    if (statusRequestPendingId_ != 0)
    {
        immediateStatusRefreshQueued_ = true;
        return;
    }

    requestStatus();
}

void RobotController::extendStatusFreshnessGrace(int durationMs)
{
    if (durationMs <= 0)
    {
        return;
    }

    statusFreshnessGraceDeadlineMs_ = qMax(
        statusFreshnessGraceDeadlineMs_,
        monotonicClock_.elapsed() + durationMs);
}

void RobotController::prepareRobot()
{
    applyTransition(state_->prepare(*this));
}

void RobotController::togglePower()
{
    toggleComponent(RobotComponent::Power);
}

void RobotController::toggleServo()
{
    toggleComponent(RobotComponent::Servo);
}

void RobotController::toggleStream()
{
    toggleComponent(RobotComponent::Stream);
}

void RobotController::setPower(bool enabled)
{
    requestComponentTarget(RobotComponent::Power, enabled);
}

void RobotController::setServo(bool enabled)
{
    requestComponentTarget(RobotComponent::Servo, enabled);
}

void RobotController::setStream(bool enabled)
{
    requestComponentTarget(RobotComponent::Stream, enabled);
}

void RobotController::toggleComponent(RobotComponent component)
{
    const ComponentRuntime &model = runtime(component);

    if (!isConfirmedComponentState(model.confirmed))
    {
        rejectAction(
            QStringLiteral("%1 state is UNKNOWN; wait for a canonical status response.")
                .arg(componentName(component)));
        forceStatusRefresh();
        emitSystemConfiguration();
        return;
    }

    requestComponentTarget(
        component,
        model.confirmed == ComponentState::Off);
}

bool RobotController::requestComponentTarget(
    RobotComponent component,
    bool enabled,
    bool preparationStep)
{
    if (!state_->isConnected()
        || (!preparationStep
            && !state_->canChangeSystemConfiguration()))
    {
        rejectAction(
            QStringLiteral("Cannot change %1 while controller state is %2.")
                .arg(componentName(component), state_->name()));
        emitSystemConfiguration();
        return false;
    }

    ComponentRuntime &model = runtime(component);

    if (model.localPending || isPendingComponentState(model.remoteState))
    {
        rejectAction(
            QStringLiteral("%1 already has a pending transition.")
                .arg(componentName(component)));
        emitSystemConfiguration();
        return false;
    }

    if (!isConfirmedComponentState(model.confirmed))
    {
        rejectAction(
            QStringLiteral("%1 state is UNKNOWN; refusing to guess a target.")
                .arg(componentName(component)));
        forceStatusRefresh();
        emitSystemConfiguration();
        return false;
    }

    if ((component == RobotComponent::Servo
         || component == RobotComponent::Stream)
        && enabled
        && !componentIsConfirmedOn(RobotComponent::Power))
    {
        rejectAction(
            QStringLiteral("Power must be confirmed ON before enabling %1.")
                .arg(componentName(component)));
        emitSystemConfiguration();
        return false;
    }

    if (component == RobotComponent::Power
        && (servo_.localPending || stream_.localPending
            || isPendingComponentState(servo_.remoteState)
            || isPendingComponentState(stream_.remoteState)))
    {
        rejectAction(
            QStringLiteral("Power cannot change while Servo or Control Manager is pending."));
        emitSystemConfiguration();
        return false;
    }

    if ((component == RobotComponent::Servo
         || component == RobotComponent::Stream)
        && (power_.localPending
            || isPendingComponentState(power_.remoteState)))
    {
        rejectAction(
            QStringLiteral("%1 cannot change while Power is pending.")
                .arg(componentName(component)));
        emitSystemConfiguration();
        return false;
    }

    const ComponentState target =
        enabled ? ComponentState::On : ComponentState::Off;
    if (model.confirmed == target)
    {
        return true;
    }

    const QString operationName =
        QStringLiteral("%1 %2")
            .arg(
                componentName(component),
                enabled ? QStringLiteral("ON") : QStringLiteral("OFF"));

    const quint64 requestId = client_->setComponent(
        component, enabled, operationName, kCommandTimeoutMs);

    if (requestId == 0)
    {
        emitSystemConfiguration();
        return false;
    }

    model.localPending = true;
    model.targetEnabled = enabled;
    model.commandRequestId = requestId;
    model.commandTimedOut = false;
    model.confirmationDeadlineMs =
        monotonicClock_.elapsed() + kConfirmationTimeoutMs;

    emit logMessage(
        QStringLiteral("%1 sent; waiting for status.components confirmation.")
            .arg(operationName));
    emitSystemConfiguration();
    return true;
}

void RobotController::cancelControl()
{
    stopVelocityInternal();

    if (state_->isConnected())
    {
        sendSimpleInternal(
            QStringLiteral("cancel"),
            QStringLiteral("Cancel control"));

        preparationRequested_ = false;
        transitionTo(std::make_unique<ConnectedState>());
    }
}

void RobotController::startDrive(
    double linearX,
    double linearY,
    double angularZ)
{
    applyTransition(
        state_->startDrive(*this, linearX, linearY, angularZ));
}

void RobotController::stopDrive()
{
    applyTransition(state_->stopDrive(*this));
}

void RobotController::refreshJoints()
{
    if (!client_->isConnected() || jointStatusRequestPendingId_ != 0)
    {
        return;
    }

    jointStatusRequestPendingId_ = client_->readJoints(kCommandTimeoutMs);
}

void RobotController::nudgeJoint(
    const QString &groupName,
    int jointIndex,
    double delta,
    double minimumTime)
{
    if (!client_->isConnected() || !state_->canControlJoints())
    {
        reportJointMotionFailure(QStringLiteral(
            "Robot chưa sẵn sàng hoặc đang thực hiện một lệnh khác. Không thể thay đổi góc khớp."));
        return;
    }

    applyTransition(
        state_->nudgeJoint(
            *this,
            groupName,
            jointIndex,
            delta,
            minimumTime));
}

void RobotController::moveJointTo(const QString &groupName, int jointIndex,
                                double targetRadians, double minimumTime)
{
    if (!client_->isConnected() || !state_->canControlJoints()
        || !qIsFinite(targetRadians) || !qIsFinite(minimumTime)
        || minimumTime < 0.0 || jointIndex < 0)
    {
        reportJointMotionFailure(QStringLiteral("Robot is not ready or the joint target is invalid."));
        return;
    }
    stopVelocityInternal();
    // Read a new snapshot, rather than deriving a relative move from the
    // UI's previous poll. Subsequent segments also use measured positions.
    const quint64 requestId = requestJointSnapshotInternal();
    if (requestId == 0)
    {
        reportJointMotionFailure(QStringLiteral("Could not read the joint position before moving."));
        return;
    }
    applyTransition(std::make_unique<JointBusyState>(
        requestId, groupName, jointIndex, targetRadians, minimumTime));
}

quint64 RobotController::requestJointSnapshotInternal()
{
    return client_->readJoints(kCommandTimeoutMs);
}

void RobotController::sendPose(
    const QString &command,
    const QString &operationName,
    double minimumTime)
{
    applyTransition(
        state_->sendPose(
            *this,
            command,
            operationName,
            minimumTime));
}

bool RobotController::sendSimpleInternal(
    const QString &command,
    const QString &operationName)
{
    return client_->executeSimple(command, operationName, kCommandTimeoutMs) != 0;
}

bool RobotController::beginPreparationInternal()
{
    if (!state_->isConnected())
    {
        rejectAction(QStringLiteral("Not connected to the robot SDK endpoint."));
        return false;
    }

    if (!isConfirmedComponentState(power_.confirmed)
        || !isConfirmedComponentState(servo_.confirmed)
        || !isConfirmedComponentState(stream_.confirmed))
    {
        rejectAction(
            QStringLiteral("Cannot prepare until Power, Servo, and Control Manager are known."));
        forceStatusRefresh();
        return false;
    }

    preparationRequested_ = true;
    QTimer::singleShot(0, this, &RobotController::continuePreparation);
    return true;
}

void RobotController::continuePreparation()
{
    if (!preparationRequested_ || !state_->isConnected())
    {
        return;
    }

    if (anyComponentPending())
    {
        forceStatusRefresh();
        return;
    }

    if (!componentIsConfirmedOn(RobotComponent::Power))
    {
        if (!requestComponentTarget(RobotComponent::Power, true, true))
        {
            abortPreparation(QStringLiteral("Power could not be enabled."));
        }
        return;
    }

    if (!componentIsConfirmedOn(RobotComponent::Servo))
    {
        if (!requestComponentTarget(RobotComponent::Servo, true, true))
        {
            abortPreparation(QStringLiteral("Servo could not be enabled."));
        }
        return;
    }

    if (!componentIsConfirmedOn(RobotComponent::Stream))
    {
        if (!requestComponentTarget(RobotComponent::Stream, true, true))
        {
            abortPreparation(QStringLiteral("Control Manager could not be enabled."));
        }
        return;
    }

    preparationRequested_ = false;
    appendStateLog(
        QStringLiteral("Preparation confirmed by status.components; robot is Ready."));
    transitionTo(std::make_unique<ReadyState>());
}

void RobotController::abortPreparation(const QString &reason)
{
    if (!preparationRequested_)
    {
        return;
    }

    preparationRequested_ = false;
    emit logMessage(QStringLiteral("Preparation stopped: %1").arg(reason));

    if (state_->name() == QStringLiteral("Preparing"))
    {
        transitionTo(std::make_unique<ConnectedState>());
    }
}

void RobotController::startVelocityInternal(
    double linearX,
    double linearY,
    double angularZ)
{
    linearX_ = linearX;
    linearY_ = linearY;
    angularZ_ = angularZ;

    sendVelocityTick();
    if (!velocityTimer_.isActive())
    {
        velocityTimer_.start();
    }
}

void RobotController::stopVelocityInternal()
{
    const bool wasDriving =
        velocityTimer_.isActive()
        || linearX_ != 0.0
        || linearY_ != 0.0
        || angularZ_ != 0.0;

    velocityTimer_.stop();
    linearX_ = 0.0;
    linearY_ = 0.0;
    angularZ_ = 0.0;

    if (client_->isConnected() && wasDriving)
    {
        sendSimpleInternal(
            QStringLiteral("stop"),
            QStringLiteral("Stop base"));
    }
}

quint64 RobotController::sendJointNudgeInternal(
    const QString &groupName,
    int jointIndex,
    double delta,
    double minimumTime)
{
    const int timeoutMs = qMax(
        kCommandTimeoutMs,
        static_cast<int>(qCeil(qMax(0.0, minimumTime) * 1000.0))
            + 2000);

    const quint64 requestId = client_->moveJointRelative(
        groupName, jointIndex, delta, minimumTime, timeoutMs);

    if (requestId != 0)
    {
        // SDK motion and status calls share a worker thread. Keep the last
        // status during the bounded request window if motion polling is delayed.
        extendStatusFreshnessGrace(
            timeoutMs + kPostMotionStatusGraceMs);
    }

    return requestId;
}

quint64 RobotController::sendPoseInternal(
    const QString &command,
    const QString &operationName,
    double minimumTime)
{
    const int timeoutMs = qMax(
        kCommandTimeoutMs,
        static_cast<int>(qCeil(qMax(0.0, minimumTime) * 1000.0))
            + 2000);

    const quint64 requestId = client_->executePose(
        command, operationName, minimumTime, timeoutMs);

    if (requestId != 0)
    {
        extendStatusFreshnessGrace(
            timeoutMs + kPostMotionStatusGraceMs);
    }

    return requestId;
}

void RobotController::scheduleJointRefresh()
{
    QTimer::singleShot(300, this, &RobotController::refreshJoints);
}

void RobotController::rejectAction(const QString &reason)
{
    emit logMessage(QStringLiteral("Command rejected: %1").arg(reason));
}

void RobotController::appendStateLog(const QString &message)
{
    emit logMessage(message);
}

void RobotController::reportJointMotionFailure(const QString &message)
{
    emit logMessage(message);
    emit jointMotionFailed(message);
    scheduleJointRefresh();
}

void RobotController::sendVelocityTick()
{
    if (!client_->isConnected())
    {
        velocityTimer_.stop();
        return;
    }

    client_->setVelocity(linearX_, linearY_, angularZ_);
}

void RobotController::handleRobotConnected()
{
    statusRequestPendingId_ = 0;
    jointStatusRequestPendingId_ = 0;
    latestAppliedJointStatusRequestId_ = 0;
    latestAppliedStatusRequestId_ = 0;
    immediateStatusRefreshQueued_ = false;
    lastValidStatusMs_ = -1;
    statusFreshnessGraceDeadlineMs_ = 0;
    statusMarkedStale_ = true;
    preparationRequested_ = false;
    markAllComponentsUnknown(true);

    transitionTo(std::make_unique<ConnectedState>());
    requestStatus();
    refreshJoints();
    statusTimer_.start();
    jointStatusTimer_.start();
    healthTimer_.start();

    const QString modelName = activeModel_ == Rby1Model::M ? QStringLiteral("M") : QStringLiteral("A");
    emit logMessage(QStringLiteral("Connected directly using the Rainbow Robotics SDK (model %1).")
                        .arg(modelName));
}

void RobotController::handleRobotDisconnected()
{
    if (state_->name() == QStringLiteral("JointBusy"))
    {
        reportJointMotionFailure(QStringLiteral(
            "Mất kết nối SDK trong khi di chuyển khớp; chưa xác nhận được kết quả."));
    }
    velocityTimer_.stop();
    statusTimer_.stop();
    jointStatusTimer_.stop();
    healthTimer_.stop();
    statusRequestPendingId_ = 0;
    jointStatusRequestPendingId_ = 0;
    immediateStatusRefreshQueued_ = false;
    preparationRequested_ = false;
    lastValidStatusMs_ = -1;
    statusFreshnessGraceDeadlineMs_ = 0;
    statusMarkedStale_ = true;

    markAllComponentsUnknown(true);
    transitionTo(std::make_unique<DisconnectedState>());
    emit logMessage(QStringLiteral("Disconnected from the robot SDK endpoint."));
}

void RobotController::handleResponse(
    quint64 requestId,
    const QString &operationName,
    const QJsonObject &response)
{
    if (operationName == QStringLiteral("Status"))
    {
        if (requestId == statusRequestPendingId_)
        {
            statusRequestPendingId_ = 0;
        }

        if (requestId == 0 || requestId >= latestAppliedStatusRequestId_)
        {
            applySystemStatus(requestId, response);
            // Component confirmation must be applied before deciding whether
            // the controller itself may enter Ready.
            updateStateFromStatus(response);
        }
    }
    else
    {
        finishComponentCommand(requestId, response);

        if (operationName == QStringLiteral("Joints status"))
        {
            if (requestId == jointStatusRequestPendingId_)
            {
                jointStatusRequestPendingId_ = 0;
            }

            if ((requestId == 0
                 || requestId > latestAppliedJointStatusRequestId_)
                && (!response.contains(QStringLiteral("success"))
                    || response.value(QStringLiteral("success")).toBool(false)))
            {
                if (requestId != 0)
                {
                    latestAppliedJointStatusRequestId_ = requestId;
                }
                emit jointStatusReceived(response);
            }
        }

        const bool wasBusy = state_->isBusy();
        std::unique_ptr<RobotState> nextState =
            state_->onResponse(
                *this,
                requestId,
                operationName,
                response);
        const bool motionFinished = wasBusy && nextState != nullptr;

        applyTransition(std::move(nextState));

        if (motionFinished)
        {
            extendStatusFreshnessGrace(kPostMotionStatusGraceMs);
            forceStatusRefresh();
        }
    }

    emit responseReceived(operationName, response);

    if (operationName == QStringLiteral("Status")
        && immediateStatusRefreshQueued_)
    {
        immediateStatusRefreshQueued_ = false;
        QTimer::singleShot(0, this, &RobotController::requestStatus);
    }
}

void RobotController::finishComponentCommand(
    quint64 requestId,
    const QJsonObject &response)
{
    const QList<RobotComponent> components{
        RobotComponent::Power,
        RobotComponent::Servo,
        RobotComponent::Stream
    };

    for (RobotComponent component : components)
    {
        ComponentRuntime &model = runtime(component);
        if (!model.localPending || model.commandRequestId != requestId)
        {
            continue;
        }

        const bool success =
            response.value(QStringLiteral("success")).isBool()
            && response.value(QStringLiteral("success")).toBool();

        if (!success)
        {
            model.localPending = false;
            model.commandRequestId = 0;
            model.confirmationDeadlineMs = 0;
            emit logMessage(
                QStringLiteral("%1 command failed: %2")
                    .arg(componentName(component), responseError(response)));
            emitSystemConfiguration();

            if (preparationRequested_)
            {
                abortPreparation(
                    QStringLiteral("%1 command failed: %2")
                        .arg(componentName(component), responseError(response)));
            }
        }
        else
        {
            model.commandRequestId = 0;
            model.confirmationDeadlineMs =
                monotonicClock_.elapsed() + kConfirmationTimeoutMs;
            emit logMessage(
                QStringLiteral("%1 command accepted; still waiting for canonical status.")
                    .arg(componentName(component)));
        }

        forceStatusRefresh();
        return;
    }
}

void RobotController::handleRequestTimeout(
    quint64 requestId,
    const QString &operationName)
{
    if (operationName == QStringLiteral("Joints status"))
    {
        latestAppliedJointStatusRequestId_ = qMax(
            latestAppliedJointStatusRequestId_, requestId);
        if (requestId == jointStatusRequestPendingId_)
        {
            jointStatusRequestPendingId_ = 0;
        }
        auto nextState = state_->onRequestTimeout(*this, requestId, operationName);
        if (nextState)
        {
            extendStatusFreshnessGrace(kPostMotionStatusGraceMs);
            transitionTo(std::move(nextState));
            forceStatusRefresh();
        }
        // Retry on the next polling tick, not recursively on every timeout.
        return;
    }

    if (requestId == statusRequestPendingId_)
    {
        statusRequestPendingId_ = 0;
        emit logMessage(QStringLiteral("Status request timed out."));
        forceStatusRefresh();
        return;
    }

    const QList<RobotComponent> components{
        RobotComponent::Power,
        RobotComponent::Servo,
        RobotComponent::Stream
    };

    for (RobotComponent component : components)
    {
        ComponentRuntime &model = runtime(component);
        if (!model.localPending || model.commandRequestId != requestId)
        {
            continue;
        }

        model.commandTimedOut = true;
        emit logMessage(
            QStringLiteral("%1 command acknowledgement timed out; status confirmation is still pending.")
                .arg(componentName(component)));
        forceStatusRefresh();
        return;
    }

    std::unique_ptr<RobotState> timeoutTransition =
        state_->onRequestTimeout(
            *this,
            requestId,
            operationName);

    if (timeoutTransition)
    {
        extendStatusFreshnessGrace(kPostMotionStatusGraceMs);
        transitionTo(std::move(timeoutTransition));
        forceStatusRefresh();
        return;
    }

    emit logMessage(
        QStringLiteral("%1 request timed out.").arg(operationName));
}

void RobotController::applySystemStatus(
    quint64 requestId,
    const QJsonObject &response)
{
    if (requestId != 0 && requestId < latestAppliedStatusRequestId_)
    {
        return;
    }

    const ParsedSystemStatus parsed = parseSystemStatus(response);
    if (!parsed.accepted)
    {
        return;
    }

    if (requestId != 0)
    {
        latestAppliedStatusRequestId_ = requestId;
    }

    lastValidStatusMs_ = monotonicClock_.elapsed();
    if (!state_->isBusy())
    {
        statusFreshnessGraceDeadlineMs_ = 0;
    }
    statusMarkedStale_ = false;

    if (!parsed.robotConnected)
    {
        markAllComponentsUnknown(true);
        if (state_->name() == QStringLiteral("Ready"))
        {
            transitionTo(std::make_unique<ConnectedState>());
        }
        abortPreparation(
            QStringLiteral("Robot SDK reports that the robot is disconnected."));
        return;
    }

    applyParsedComponent(RobotComponent::Power, parsed.power);
    applyParsedComponent(RobotComponent::Servo, parsed.servo);
    applyParsedComponent(RobotComponent::Stream, parsed.stream);
    emitSystemConfiguration();

    if (state_->name() == QStringLiteral("Ready")
        && (!componentIsConfirmedOn(RobotComponent::Power)
            || !componentIsConfirmedOn(RobotComponent::Servo)
            || !componentIsConfirmedOn(RobotComponent::Stream)
            || anyComponentPending()))
    {
        transitionTo(std::make_unique<ConnectedState>());
    }

    if (preparationRequested_)
    {
        continuePreparation();
    }
}

void RobotController::applyParsedComponent(
    RobotComponent component,
    const ComponentStatus &status)
{
    ComponentRuntime &model = runtime(component);
    model.remoteState = status.state;
    model.source = status.source;
    model.legacy = status.legacy;

    if (status.state == ComponentState::Unknown)
    {
        model.confirmed = ComponentState::Unknown;
        return;
    }

    if (isPendingComponentState(status.state))
    {
        return;
    }

    model.confirmed = status.state;

    if (model.localPending)
    {
        const ComponentState expected =
            model.targetEnabled ? ComponentState::On : ComponentState::Off;

        if (status.state == expected)
        {
            model.localPending = false;
            model.commandRequestId = 0;
            model.commandTimedOut = false;
            model.confirmationDeadlineMs = 0;
            emit logMessage(
                QStringLiteral("%1 is confirmed %2 by status.components.")
                    .arg(
                        componentName(component),
                        model.targetEnabled
                            ? QStringLiteral("ON")
                            : QStringLiteral("OFF")));
        }
    }
}

void RobotController::checkTimeoutsAndFreshness()
{
    if (!client_->isConnected())
    {
        return;
    }

    const qint64 now = monotonicClock_.elapsed();
    bool configurationChanged = false;
    bool preparationTimedOut = false;

    const QList<RobotComponent> components{
        RobotComponent::Power,
        RobotComponent::Servo,
        RobotComponent::Stream
    };

    for (RobotComponent component : components)
    {
        ComponentRuntime &model = runtime(component);
        if (!model.localPending
            || model.confirmationDeadlineMs <= 0
            || now < model.confirmationDeadlineMs)
        {
            continue;
        }

        model.localPending = false;
        model.commandRequestId = 0;
        model.commandTimedOut = false;
        model.confirmationDeadlineMs = 0;
        configurationChanged = true;
        preparationTimedOut = preparationTimedOut || preparationRequested_;
        emit logMessage(
            QStringLiteral("Timed out waiting for %1 status confirmation; restored the latest confirmed state.")
                .arg(componentName(component)));
    }

    if (configurationChanged)
    {
        emitSystemConfiguration();
    }

    if (preparationTimedOut)
    {
        abortPreparation(
            QStringLiteral("Timed out waiting for component confirmation."));
    }

    const qint64 statusStaleDeadlineMs =
        lastValidStatusMs_ + kStatusStaleMs;
    const qint64 effectiveStaleDeadlineMs = qMax(
        statusStaleDeadlineMs,
        statusFreshnessGraceDeadlineMs_);

    if (!statusMarkedStale_
        && lastValidStatusMs_ >= 0
        && now >= effectiveStaleDeadlineMs)
    {
        statusFreshnessGraceDeadlineMs_ = 0;
        statusMarkedStale_ = true;
        markAllComponentsUnknown(true);
        if (state_->name() == QStringLiteral("Ready"))
        {
            transitionTo(std::make_unique<ConnectedState>());
        }
        abortPreparation(
            QStringLiteral("Canonical status became stale."));
        emit logMessage(
            QStringLiteral("Component status is stale; Power, Servo, and Control Manager are UNKNOWN."));
    }
}

void RobotController::markAllComponentsUnknown(bool clearPending)
{
    const auto clear =
        [clearPending](ComponentRuntime &model)
        {
            model.confirmed = ComponentState::Unknown;
            model.remoteState = ComponentState::Unknown;
            model.source.clear();
            model.legacy = false;

            if (clearPending)
            {
                model.localPending = false;
                model.targetEnabled = false;
                model.commandRequestId = 0;
                model.commandTimedOut = false;
                model.confirmationDeadlineMs = 0;
            }
        };

    clear(power_);
    clear(servo_);
    clear(stream_);
    emitSystemConfiguration();
}

SystemConfigurationView RobotController::systemConfiguration() const
{
    return {
        makeView(power_),
        makeView(servo_),
        makeView(stream_)
    };
}

ComponentViewState RobotController::makeView(
    const ComponentRuntime &component) const
{
    ComponentState displayState = component.confirmed;

    if (component.remoteState == ComponentState::Unknown)
    {
        displayState = ComponentState::Unknown;
    }
    else if (isPendingComponentState(component.remoteState))
    {
        displayState = component.remoteState;
    }
    else if (component.localPending)
    {
        displayState = component.targetEnabled
            ? ComponentState::PendingOn
            : ComponentState::PendingOff;
    }

    return {
        displayState,
        component.confirmed,
        component.source,
        component.legacy
    };
}

void RobotController::emitSystemConfiguration()
{
    emit systemConfigurationChanged(systemConfiguration());
}

RobotController::ComponentRuntime &RobotController::runtime(
    RobotComponent component)
{
    switch (component)
    {
    case RobotComponent::Power:
        return power_;
    case RobotComponent::Servo:
        return servo_;
    case RobotComponent::Stream:
        return stream_;
    }

    return power_;
}

const RobotController::ComponentRuntime &RobotController::runtime(
    RobotComponent component) const
{
    switch (component)
    {
    case RobotComponent::Power:
        return power_;
    case RobotComponent::Servo:
        return servo_;
    case RobotComponent::Stream:
        return stream_;
    }

    return power_;
}

bool RobotController::componentIsConfirmedOn(
    RobotComponent component) const
{
    return runtime(component).confirmed == ComponentState::On;
}

bool RobotController::anyComponentPending() const
{
    const auto pending =
        [](const ComponentRuntime &model)
        {
            return model.localPending
                || isPendingComponentState(model.remoteState);
        };

    return pending(power_) || pending(servo_) || pending(stream_);
}

void RobotController::transitionTo(
    std::unique_ptr<RobotState> nextState)
{
    if (!nextState)
    {
        return;
    }

    const QString oldName =
        state_ ? state_->name() : QStringLiteral("<none>");
    const QString newName = nextState->name();
    state_ = std::move(nextState);

    emit logMessage(
        QStringLiteral("STATE: %1 -> %2").arg(oldName, newName));
    emitCurrentState();
}

void RobotController::applyTransition(
    std::unique_ptr<RobotState> nextState)
{
    if (nextState)
    {
        transitionTo(std::move(nextState));
    }
}

void RobotController::emitCurrentState()
{
    emit stateChanged(
        state_->name(),
        state_->isConnected(),
        state_->canDrive(),
        state_->canControlJoints(),
        state_->canChangeSystemConfiguration(),
        state_->isBusy());
}

void RobotController::updateStateFromStatus(
    const QJsonObject &response)
{
    if (!response.value(QStringLiteral("success")).toBool(false))
    {
        return;
    }

    QJsonValue readyValue = response.value(QStringLiteral("ready"));
    if (!readyValue.isBool()
        && response.value(QStringLiteral("status")).isObject())
    {
        readyValue = response.value(QStringLiteral("status"))
                         .toObject()
                         .value(QStringLiteral("ready"));
    }

    if (!readyValue.isBool())
    {
        return;
    }

    if (readyValue.toBool())
    {
        const bool configurationConfirmedReady =
            componentIsConfirmedOn(RobotComponent::Power)
            && componentIsConfirmedOn(RobotComponent::Servo)
            && componentIsConfirmedOn(RobotComponent::Stream)
            && !anyComponentPending();

        if (state_->name() == QStringLiteral("Connected")
            && configurationConfirmedReady)
        {
            appendStateLog(
                QStringLiteral(
                    "Robot SDK status confirmed Ready with Power, Servo, and Control Manager ON."));
            transitionTo(std::make_unique<ReadyState>());
        }

        return;
    }

    if (state_->name() == QStringLiteral("Ready"))
    {
        transitionTo(std::make_unique<ConnectedState>());
    }
}
