#include "controller/RobotController.hpp"

#include "network/RobotClient.hpp"

#include "state/ConnectedState.hpp"
#include "state/DisconnectedState.hpp"
#include "state/ReadyState.hpp"
#include "state/RobotState.hpp"

#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

namespace
{
QJsonValue findSystemStatusValue(
    const QJsonObject &response,
    const QStringList &keys)
{
    const QJsonObject status =
        response.value(QStringLiteral("status")).toObject();

    for (const QString &key : keys)
    {
        if (response.contains(key))
        {
            return response.value(key);
        }

        if (status.contains(key))
        {
            return status.value(key);
        }
    }

    return {};
}
}

RobotController::RobotController(QObject *parent)
    : QObject(parent),
      client_(new RobotClient(this)),
      state_(std::make_unique<DisconnectedState>())
{
    velocityTimer_.setInterval(100);
    statusTimer_.setInterval(500);

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
        client_,
        &RobotClient::bridgeConnected,
        this,
        &RobotController::handleBridgeConnected);

    connect(
        client_,
        &RobotClient::bridgeDisconnected,
        this,
        &RobotController::handleBridgeDisconnected);

    connect(
        client_,
        &RobotClient::responseReceived,
        this,
        &RobotController::handleResponse);

    connect(
        client_,
        &RobotClient::clientError,
        this,
        [this](const QString &message)
        {
            emit logMessage(
                QStringLiteral("Lỗi TCP: %1")
                    .arg(message));
        });
}

RobotController::~RobotController() = default;

void RobotController::connectToBridge()
{
    if (client_->isConnected())
    {
        return;
    }

    emit logMessage(
        QStringLiteral(
            "Đang kết nối 127.0.0.1:8081..."));

    client_->connectToBridge();
}

void RobotController::disconnectFromBridge()
{
    stopVelocityInternal();
    client_->disconnectFromBridge();
}

void RobotController::ping()
{
    sendSimpleInternal(
        QStringLiteral("ping"),
        QStringLiteral("Ping"));
}

void RobotController::requestStatus()
{
    if (statusRequestPending_)
    {
        return;
    }

    statusRequestPending_ =
        sendSimpleInternal(
            QStringLiteral("status"),
            QStringLiteral("Đọc trạng thái"));
}

void RobotController::prepareRobot()
{
    applyTransition(
        state_->prepare(*this));
}

void RobotController::setPower(bool enabled)
{
    if (!state_->isConnected()
        || !state_->canChangeSystemConfiguration())
    {
        rejectAction(
            QStringLiteral(
                "Không thể đổi Power trong trạng thái %1.")
                .arg(state_->name()));
        return;
    }

    if (!enabled)
    {
        powerEnabled_ = false;
        servoEnabled_ = false;
        streamEnabled_ = false;
        emitSystemConfiguration();

        // Shut down dependent subsystems before cutting their power source.
        sendSwitchInternal(
            QStringLiteral("servo"),
            false,
            QStringLiteral("Servo OFF"));

        sendSwitchInternal(
            QStringLiteral("stream"),
            false,
            QStringLiteral("Stream OFF"));
    }

    sendSwitchInternal(
        QStringLiteral("power"),
        enabled,
        enabled
            ? QStringLiteral("Power ON")
            : QStringLiteral("Power OFF"));
}

void RobotController::setServo(bool enabled)
{
    if (!state_->isConnected()
        || !state_->canChangeSystemConfiguration())
    {
        rejectAction(
            QStringLiteral(
                "Không thể đổi Servo trong trạng thái %1.")
                .arg(state_->name()));
        return;
    }

    if (enabled && !powerEnabled_)
    {
        rejectAction(
            QStringLiteral(
                "Phải bật Power trước khi bật Servo."));
        emitSystemConfiguration();
        return;
    }

    sendSwitchInternal(
        QStringLiteral("servo"),
        enabled,
        enabled
            ? QStringLiteral("Servo ON")
            : QStringLiteral("Servo OFF"));
}

void RobotController::setStream(bool enabled)
{
    if (!state_->isConnected()
        || !state_->canChangeSystemConfiguration())
    {
        rejectAction(
            QStringLiteral(
                "Không thể đổi Stream trong trạng thái %1.")
                .arg(state_->name()));
        return;
    }

    if (enabled && !powerEnabled_)
    {
        rejectAction(
            QStringLiteral(
                "Phải bật Power trước khi bật Stream."));
        emitSystemConfiguration();
        return;
    }

    sendSwitchInternal(
        QStringLiteral("stream"),
        enabled,
        enabled
            ? QStringLiteral("Stream ON")
            : QStringLiteral("Stream OFF"));
}

void RobotController::cancelControl()
{
    stopVelocityInternal();

    if (state_->isConnected())
    {
        sendSimpleInternal(
            QStringLiteral("cancel"),
            QStringLiteral("Cancel control"));

        transitionTo(
            std::make_unique<ConnectedState>());
    }
}

void RobotController::startDrive(
    double linearX,
    double linearY,
    double angularZ)
{
    applyTransition(
        state_->startDrive(
            *this,
            linearX,
            linearY,
            angularZ));
}

void RobotController::stopDrive()
{
    applyTransition(
        state_->stopDrive(*this));
}

void RobotController::refreshJoints()
{
    if (!state_->isConnected())
    {
        rejectAction(
            QStringLiteral(
                "Chưa kết nối bridge."));
        return;
    }

    sendSimpleInternal(
        QStringLiteral("joints_status"),
        QStringLiteral("Joints status"));
}

void RobotController::nudgeJoint(
    const QString &groupName,
    int jointIndex,
    double delta,
    double minimumTime)
{
    applyTransition(
        state_->nudgeJoint(
            *this,
            groupName,
            jointIndex,
            delta,
            minimumTime));
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
    return client_->sendCommand(
        QJsonObject{
            {
                QStringLiteral("command"),
                command
            }
        },
        operationName);
}

bool RobotController::sendSwitchInternal(
    const QString &command,
    bool enabled,
    const QString &operationName)
{
    return client_->sendCommand(
        QJsonObject{
            {
                QStringLiteral("command"),
                command
            },
            {
                QStringLiteral("enabled"),
                enabled
            }
        },
        operationName);
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
    velocityTimer_.stop();

    linearX_ = 0.0;
    linearY_ = 0.0;
    angularZ_ = 0.0;

    if (client_->isConnected())
    {
        sendSimpleInternal(
            QStringLiteral("stop"),
            QStringLiteral("Dừng đế"));
    }
}

bool RobotController::sendJointNudgeInternal(
    const QString &groupName,
    int jointIndex,
    double delta,
    double minimumTime)
{
    return client_->sendCommand(
        QJsonObject{
            {
                QStringLiteral("command"),
                QStringLiteral("joint_nudge")
            },
            {
                QStringLiteral("group"),
                groupName
            },
            {
                QStringLiteral("joint_index"),
                jointIndex
            },
            {
                QStringLiteral("delta"),
                delta
            },
            {
                QStringLiteral("minimum_time"),
                minimumTime
            }
        },
        QStringLiteral("Joint nudge"));
}

bool RobotController::sendPoseInternal(
    const QString &command,
    const QString &operationName,
    double minimumTime)
{
    return client_->sendCommand(
        QJsonObject{
            {
                QStringLiteral("command"),
                command
            },
            {
                QStringLiteral("minimum_time"),
                minimumTime
            }
        },
        operationName);
}

void RobotController::scheduleJointRefresh()
{
    QTimer::singleShot(
        300,
        this,
        &RobotController::refreshJoints);
}

void RobotController::rejectAction(
    const QString &reason)
{
    emit logMessage(
        QStringLiteral("Từ chối lệnh: %1")
            .arg(reason));
}

void RobotController::appendStateLog(
    const QString &message)
{
    emit logMessage(message);
}

void RobotController::sendVelocityTick()
{
    if (!client_->isConnected())
    {
        velocityTimer_.stop();
        return;
    }

    client_->sendCommand(
        QJsonObject{
            {
                QStringLiteral("command"),
                QStringLiteral("velocity")
            },
            {
                QStringLiteral("linear_x"),
                linearX_
            },
            {
                QStringLiteral("linear_y"),
                linearY_
            },
            {
                QStringLiteral("angular_z"),
                angularZ_
            }
        },
        QStringLiteral("Velocity"));
}

void RobotController::handleBridgeConnected()
{
    transitionTo(
        std::make_unique<ConnectedState>());

    requestStatus();
    statusTimer_.start();

    emit logMessage(
        QStringLiteral(
            "Đã kết nối C++ ROS 2 bridge."));
}

void RobotController::handleBridgeDisconnected()
{
    velocityTimer_.stop();
    statusTimer_.stop();
    statusRequestPending_ = false;

    powerEnabled_ = false;
    servoEnabled_ = false;
    streamEnabled_ = false;
    emitSystemConfiguration();

    transitionTo(
        std::make_unique<DisconnectedState>());

    emit logMessage(
        QStringLiteral(
            "Đã ngắt kết nối bridge."));
}

void RobotController::handleResponse(
    const QString &operationName,
    const QJsonObject &response)
{
    if (operationName == QStringLiteral("Đọc trạng thái"))
    {
        statusRequestPending_ = false;
    }

    emit responseReceived(
        operationName,
        response);

    if (operationName == QStringLiteral("Joints status"))
    {
        emit jointStatusReceived(response);
    }

    applyTransition(
        state_->onResponse(
            *this,
            operationName,
            response));

    if (
        operationName == QStringLiteral("Đọc trạng thái")
        && !state_->isBusy()
        && state_->name() != QStringLiteral("Driving"))
    {
        updateStateFromStatus(response);
    }

    if (operationName == QStringLiteral("Đọc trạng thái"))
    {
        updateSystemConfigurationFromStatus(response);
    }

    const bool operationSucceeded =
        response.value(
            QStringLiteral("success")).toBool(false);

    const bool isSwitchOperation =
        operationName == QStringLiteral("Power ON")
        || operationName == QStringLiteral("Power OFF")
        || operationName == QStringLiteral("Servo ON")
        || operationName == QStringLiteral("Servo OFF")
        || operationName == QStringLiteral("Stream ON")
        || operationName == QStringLiteral("Stream OFF");

    if (operationSucceeded)
    {
        if (operationName == QStringLiteral("Power ON"))
        {
            powerEnabled_ = true;
            emitSystemConfiguration();
        }
        else if (operationName == QStringLiteral("Power OFF"))
        {
            powerEnabled_ = false;
            servoEnabled_ = false;
            streamEnabled_ = false;
            emitSystemConfiguration();
        }
        else if (operationName == QStringLiteral("Servo ON"))
        {
            servoEnabled_ = powerEnabled_;
            emitSystemConfiguration();
        }
        else if (operationName == QStringLiteral("Servo OFF"))
        {
            servoEnabled_ = false;
            emitSystemConfiguration();
        }
        else if (operationName == QStringLiteral("Stream ON"))
        {
            streamEnabled_ = powerEnabled_;
            emitSystemConfiguration();
        }
        else if (operationName == QStringLiteral("Stream OFF"))
        {
            streamEnabled_ = false;
            emitSystemConfiguration();
        }
    }
    else if (isSwitchOperation)
    {
        // Restore the last confirmed values after a rejected/failed command.
        emitSystemConfiguration();
    }

    if (
        operationName == QStringLiteral("Power OFF")
        || operationName == QStringLiteral("Servo OFF")
        || operationName == QStringLiteral("Stream OFF"))
    {
        if (operationSucceeded)
        {
            transitionTo(
                std::make_unique<ConnectedState>());
        }
    }
    else if (
        operationName == QStringLiteral("Power ON")
        || operationName == QStringLiteral("Servo ON")
        || operationName == QStringLiteral("Stream ON"))
    {
        if (operationSucceeded)
        {
            if (state_->name() == QStringLiteral("Connected")
                && powerEnabled_ && servoEnabled_ && streamEnabled_)
            {
                // Người dùng đã bật và bridge đã xác nhận đủ ba hệ thống.
                prepareRobot();
            }
        }
    }
}

void RobotController::transitionTo(
    std::unique_ptr<RobotState> nextState)
{
    if (!nextState)
    {
        return;
    }

    const QString oldName =
        state_ ? state_->name()
               : QStringLiteral("<none>");

    const QString newName =
        nextState->name();

    state_ = std::move(nextState);

    emit logMessage(
        QStringLiteral("STATE: %1 -> %2")
            .arg(oldName, newName));

    emitCurrentState();
}

void RobotController::applyTransition(
    std::unique_ptr<RobotState> nextState)
{
    if (nextState)
    {
        transitionTo(
            std::move(nextState));
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
    if (!response.contains(QStringLiteral("ready")))
    {
        return;
    }

    const bool success =
        response.value(
            QStringLiteral("success")).toBool(false);

    if (!success)
    {
        return;
    }

    const bool ready =
        response.value(
            QStringLiteral("ready")).toBool(false);

    if (ready)
    {
        // Status chỉ xác nhận trạng thái của bridge. Không tự nâng ứng dụng
        // từ Connected lên Ready; người dùng phải nhấn Chuẩn bị robot.
        return;
    }

    if (state_->name() == QStringLiteral("Ready"))
    {
        transitionTo(
            std::make_unique<ConnectedState>());
    }
}

void RobotController::updateSystemConfigurationFromStatus(
    const QJsonObject &response)
{
    if (!response.value(
            QStringLiteral("success")).toBool(false))
    {
        return;
    }

    const QJsonValue powerValue =
        findSystemStatusValue(
            response,
            {
                QStringLiteral("power"),
                QStringLiteral("power_on"),
                QStringLiteral("powered")
            });

    const QJsonValue servoValue =
        findSystemStatusValue(
            response,
            {
                QStringLiteral("servo"),
                QStringLiteral("servo_on"),
                QStringLiteral("servo_enabled")
            });

    const QJsonValue streamValue =
        findSystemStatusValue(
            response,
            {
                QStringLiteral("stream"),
                QStringLiteral("stream_on"),
                QStringLiteral("streaming"),
                QStringLiteral("stream_enabled")
            });

    if (powerValue.isBool())
    {
        powerEnabled_ = powerValue.toBool();
    }

    if (servoValue.isBool())
    {
        servoEnabled_ = servoValue.toBool();
    }

    if (streamValue.isBool())
    {
        streamEnabled_ = streamValue.toBool();
    }

    if (!powerEnabled_)
    {
        servoEnabled_ = false;
        streamEnabled_ = false;
    }

    emitSystemConfiguration();
}

void RobotController::emitSystemConfiguration()
{
    emit systemConfigurationChanged(
        powerEnabled_,
        servoEnabled_,
        streamEnabled_);
}
