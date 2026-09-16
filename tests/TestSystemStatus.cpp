#include "controller/RobotController.hpp"
#include "model/SystemStatus.hpp"
#include "network/NdjsonParser.hpp"
#include "state/JointBusyState.hpp"
#include "ui/ToggleSwitch.hpp"
#include "ui/MainWindow.hpp"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QGroupBox>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
#include <QPointer>
#include <QQueue>
#include <QSignalBlocker>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

namespace
{
QJsonObject componentObject(ComponentState state, const QString &source)
{
    switch (state)
    {
    case ComponentState::On:
        return {
            {QStringLiteral("known"), true},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("pending"), false},
            {QStringLiteral("source"), source}
        };
    case ComponentState::Off:
        return {
            {QStringLiteral("known"), true},
            {QStringLiteral("enabled"), false},
            {QStringLiteral("pending"), false},
            {QStringLiteral("source"), source}
        };
    case ComponentState::PendingOn:
        return {
            {QStringLiteral("known"), true},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("pending"), true},
            {QStringLiteral("source"), source}
        };
    case ComponentState::PendingOff:
        return {
            {QStringLiteral("known"), true},
            {QStringLiteral("enabled"), false},
            {QStringLiteral("pending"), true},
            {QStringLiteral("source"), source}
        };
    case ComponentState::Unknown:
    default:
        return {
            {QStringLiteral("known"), false},
            {QStringLiteral("enabled"), false},
            {QStringLiteral("pending"), false},
            {QStringLiteral("source"), source}
        };
    }
}

QJsonObject statusResponse(
    ComponentState power,
    ComponentState servo,
    ComponentState stream,
    bool connected = true,
    bool ready = false)
{
    return {
        {QStringLiteral("success"), true},
        {QStringLiteral("connected"), connected},
        {QStringLiteral("ready"), ready},
        {
            QStringLiteral("components"),
            QJsonObject{
                {
                    QStringLiteral("power"),
                    componentObject(power, QStringLiteral("robot_api"))
                },
                {
                    QStringLiteral("servo"),
                    componentObject(servo, QStringLiteral("robot_api"))
                },
                {
                    QStringLiteral("stream"),
                    componentObject(stream, QStringLiteral("robot_state"))
                }
            }
        }
    };
}

class FakeBridge final : public QObject
{
public:
    explicit FakeBridge(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(
            &server_,
            &QTcpServer::newConnection,
            this,
            [this]()
            {
                socket_ = server_.nextPendingConnection();
                receiveBuffer_.clear();

                QTcpSocket *connectedSocket = socket_;
                connect(
                    connectedSocket,
                    &QTcpSocket::readyRead,
                    this,
                    [this, connectedSocket]()
                    {
                        receiveBuffer_.append(connectedSocket->readAll());
                        parseCommands();
                    });
            });
    }

    bool listen()
    {
        return server_.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const
    {
        return server_.serverPort();
    }

    bool hasCommand(const QString &name) const
    {
        for (const QJsonObject &command : commands_)
        {
            if (command.value(QStringLiteral("command")).toString() == name)
            {
                return true;
            }
        }

        return false;
    }

    QJsonObject takeCommand(const QString &name)
    {
        for (qsizetype index = 0; index < commands_.size(); ++index)
        {
            if (commands_.at(index)
                    .value(QStringLiteral("command"))
                    .toString()
                == name)
            {
                return commands_.takeAt(index);
            }
        }

        return {};
    }

    void clearCommands()
    {
        commands_.clear();
    }

    void reply(const QJsonObject &response)
    {
        QVERIFY(socket_);
        QByteArray payload =
            QJsonDocument(response).toJson(QJsonDocument::Compact);
        payload.append('\n');
        QCOMPARE(socket_->write(payload), payload.size());
        socket_->flush();
    }

private:
    void parseCommands()
    {
        while (true)
        {
            const qsizetype newline = receiveBuffer_.indexOf('\n');
            if (newline < 0)
            {
                return;
            }

            const QByteArray line = receiveBuffer_.left(newline).trimmed();
            receiveBuffer_.remove(0, newline + 1);

            const QJsonDocument document = QJsonDocument::fromJson(line);
            if (document.isObject())
            {
                commands_.enqueue(document.object());
            }
        }
    }

    QTcpServer server_;
    QPointer<QTcpSocket> socket_;
    QByteArray receiveBuffer_;
    QQueue<QJsonObject> commands_;
};

void connectAndReplyInitialStatus(
    RobotController &controller,
    FakeBridge &bridge,
    const QJsonObject &response)
{
    controller.connectToBridge(QStringLiteral("127.0.0.1"), bridge.port());
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("status")), 1000);
    bridge.takeCommand(QStringLiteral("status"));
    bridge.reply(response);
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.systemConfiguration().power.state
            != ComponentState::Unknown
            || response.value(QStringLiteral("connected")).toBool(true)
                == false,
        1000);
}
}

class TestSystemStatus final : public QObject
{
    Q_OBJECT

private slots:
    void parsesCanonicalStatesAndMissingFields();
    void parsesLegacyWithoutTurningMissingIntoOff();
    void parsesPartialAndMultipleNdjsonFrames();
    void programmaticCheckedUpdateDoesNotClick();
    void commandSuccessStillWaitsForStatus();
    void commandFailureRestoresConfirmedStateAndReportsError();
    void canonicalReadyAutomaticallyEnablesController();
    void jointAckCanArriveBeforeOutstandingStatus();
    void longMotionKeepsCanonicalComponentStatusDuringBridgeSilence();
    void largeJointMoveUsesOneTotalDurationAndKeepsStreamOn();
    void jointBusyIgnoresStaleResponseAndRecoversOnTimeout();
    void disconnectBecomesUnknownAndReconnectResynchronizes();
    void externalClientChangesAreReflectedByPolling();
    void jointTimerPollsDuringMotionAndRestartsOnReconnect();
    void jointTimerRetriesTimeoutAndIgnoresLateSnapshot();
    void continuousJointDisplayDoesNotInterruptDraggingOrSendCommands();
    void compactLayoutKeepsAllJointRowsVisible();
    void jointAngleEntrySubmitsOnlyOnEnterAndReportsFailures();
    void jointAngleEntryRejectsInvalidTargetsAndTimeoutReportsFailure();
};

void TestSystemStatus::parsesCanonicalStatesAndMissingFields()
{
    ParsedSystemStatus parsed = parseSystemStatus(
        statusResponse(
            ComponentState::On,
            ComponentState::Off,
            ComponentState::Unknown));

    QVERIFY(parsed.accepted);
    QVERIFY(parsed.canonical);
    QCOMPARE(parsed.power.state, ComponentState::On);
    QCOMPARE(parsed.servo.state, ComponentState::Off);
    QCOMPARE(parsed.stream.state, ComponentState::Unknown);

    parsed = parseSystemStatus(
        statusResponse(
            ComponentState::PendingOn,
            ComponentState::PendingOff,
            ComponentState::On));
    QCOMPARE(parsed.power.state, ComponentState::PendingOn);
    QCOMPARE(parsed.servo.state, ComponentState::PendingOff);

    QJsonObject missingEnabled = statusResponse(
        ComponentState::On,
        ComponentState::Off,
        ComponentState::Off);
    QJsonObject components =
        missingEnabled.value(QStringLiteral("components")).toObject();
    QJsonObject power =
        components.value(QStringLiteral("power")).toObject();
    power.remove(QStringLiteral("enabled"));
    components.insert(QStringLiteral("power"), power);
    missingEnabled.insert(QStringLiteral("components"), components);

    parsed = parseSystemStatus(missingEnabled);
    QCOMPARE(parsed.power.state, ComponentState::Unknown);
    QCOMPARE(parsed.servo.state, ComponentState::Off);

    const ParsedSystemStatus malformedCanonical = parseSystemStatus(
        QJsonObject{
            {QStringLiteral("success"), true},
            {QStringLiteral("components"), QStringLiteral("invalid")},
            {QStringLiteral("power"), true}
        });
    QVERIFY(malformedCanonical.canonical);
    QCOMPARE(
        malformedCanonical.power.state,
        ComponentState::Unknown);
}

void TestSystemStatus::parsesLegacyWithoutTurningMissingIntoOff()
{
    const ParsedSystemStatus parsed = parseSystemStatus(
        QJsonObject{
            {QStringLiteral("success"), true},
            {QStringLiteral("power"), false},
            {QStringLiteral("servo_enabled"), true}
        });

    QVERIFY(parsed.accepted);
    QVERIFY(!parsed.canonical);
    QCOMPARE(parsed.power.state, ComponentState::Off);
    QCOMPARE(parsed.servo.state, ComponentState::On);
    QCOMPARE(parsed.stream.state, ComponentState::Unknown);
    QVERIFY(parsed.power.legacy);
    QVERIFY(parsed.stream.legacy);
}

void TestSystemStatus::parsesPartialAndMultipleNdjsonFrames()
{
    NdjsonParser parser;

    QVector<NdjsonFrame> frames = parser.append(
        QByteArrayLiteral("{\"success\":true}\n{\"success\":"));
    QCOMPARE(frames.size(), 1);
    QVERIFY(frames.at(0).valid);
    QVERIFY(!parser.bufferedData().isEmpty());

    frames = parser.append(
        QByteArrayLiteral("false}\n{\"value\":3}\n"));
    QCOMPARE(frames.size(), 2);
    QVERIFY(frames.at(0).valid);
    QVERIFY(!frames.at(0).object.value(QStringLiteral("success")).toBool());
    QCOMPARE(frames.at(1).object.value(QStringLiteral("value")).toInt(), 3);
    QVERIFY(parser.bufferedData().isEmpty());
}

void TestSystemStatus::programmaticCheckedUpdateDoesNotClick()
{
    ToggleSwitch toggle;
    QSignalSpy clickedSpy(&toggle, &ToggleSwitch::clicked);
    QSignalSpy toggledSpy(&toggle, &ToggleSwitch::toggled);

    {
        const QSignalBlocker blocker(&toggle);
        toggle.setChecked(true);
        toggle.setComponentState(ComponentState::PendingOff);
    }

    QCOMPARE(clickedSpy.count(), 0);
    QCOMPARE(toggledSpy.count(), 0);
    QVERIFY(toggle.isChecked());
}

void TestSystemStatus::commandSuccessStillWaitsForStatus()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    RobotController controller;

    connectAndReplyInitialStatus(
        controller,
        bridge,
        statusResponse(
            ComponentState::Off,
            ComponentState::Off,
            ComponentState::Off));

    QTRY_VERIFY_WITH_TIMEOUT(
        controller.systemConfiguration().power.state == ComponentState::Off,
        1000);

    controller.togglePower();
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("power")), 1000);
    const QJsonObject command = bridge.takeCommand(QStringLiteral("power"));
    QVERIFY(command.value(QStringLiteral("enabled")).toBool());
    QCOMPARE(
        controller.systemConfiguration().power.state,
        ComponentState::PendingOn);
    QCOMPARE(
        controller.systemConfiguration().power.confirmedState,
        ComponentState::Off);

    bridge.reply(QJsonObject{{QStringLiteral("success"), true}});
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("status")), 1000);
    bridge.takeCommand(QStringLiteral("status"));
    bridge.reply(
        statusResponse(
            ComponentState::Off,
            ComponentState::Off,
            ComponentState::Off));

    QTest::qWait(50);
    QCOMPARE(
        controller.systemConfiguration().power.state,
        ComponentState::PendingOn);
    QCOMPARE(
        controller.systemConfiguration().power.confirmedState,
        ComponentState::Off);
}

void TestSystemStatus::commandFailureRestoresConfirmedStateAndReportsError()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    RobotController controller;
    QSignalSpy logSpy(&controller, &RobotController::logMessage);

    connectAndReplyInitialStatus(
        controller,
        bridge,
        statusResponse(
            ComponentState::On,
            ComponentState::Off,
            ComponentState::Off));

    QTRY_VERIFY_WITH_TIMEOUT(
        controller.systemConfiguration().power.state == ComponentState::On,
        1000);
    controller.togglePower();
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("power")), 1000);
    bridge.takeCommand(QStringLiteral("power"));

    bridge.reply(
        QJsonObject{
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), QStringLiteral("interlock active")}
        });

    QTRY_VERIFY_WITH_TIMEOUT(
        controller.systemConfiguration().power.state == ComponentState::On,
        1000);
    QCOMPARE(
        controller.systemConfiguration().power.confirmedState,
        ComponentState::On);

    bool foundSpecificError = false;
    for (const QList<QVariant> &arguments : logSpy)
    {
        foundSpecificError = foundSpecificError
            || arguments.at(0).toString().contains(
                QStringLiteral("interlock active"));
    }
    QVERIFY(foundSpecificError);
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("status")), 1000);
}

void TestSystemStatus::canonicalReadyAutomaticallyEnablesController()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    RobotController controller;
    QSignalSpy stateSpy(&controller, &RobotController::stateChanged);

    // ready=true alone is insufficient while any confirmed component is OFF.
    connectAndReplyInitialStatus(
        controller,
        bridge,
        statusResponse(
            ComponentState::On,
            ComponentState::On,
            ComponentState::Off,
            true,
            true));

    QTRY_VERIFY_WITH_TIMEOUT(!stateSpy.isEmpty(), 1000);
    QCOMPARE(
        stateSpy.last().at(0).toString(),
        QStringLiteral("Connected"));

    // The next canonical status represents the final manual Stream ON
    // confirmation. No Prepare action is involved.
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("status")), 1200);
    bridge.takeCommand(QStringLiteral("status"));
    bridge.reply(
        statusResponse(
            ComponentState::On,
            ComponentState::On,
            ComponentState::On,
            true,
            true));

    QTRY_VERIFY_WITH_TIMEOUT(
        !stateSpy.isEmpty()
            && stateSpy.last().at(0).toString()
                == QStringLiteral("Ready"),
        1000);
    QVERIFY(stateSpy.last().at(2).toBool());
    QVERIFY(stateSpy.last().at(3).toBool());
}

void TestSystemStatus::jointAckCanArriveBeforeOutstandingStatus()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    RobotController controller;
    QSignalSpy stateSpy(&controller, &RobotController::stateChanged);

    connectAndReplyInitialStatus(
        controller,
        bridge,
        statusResponse(
            ComponentState::On,
            ComponentState::On,
            ComponentState::On,
            true,
            true));
    QTRY_VERIFY_WITH_TIMEOUT(
        !stateSpy.isEmpty()
            && stateSpy.last().at(0).toString()
                == QStringLiteral("Ready"),
        1000);

    // Keep a periodic status request outstanding, then let the joint ACK
    // arrive first. This is the ordering that previously stranded JointBusy.
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("status")), 1200);
    bridge.takeCommand(QStringLiteral("status"));

    controller.nudgeJoint(
        QStringLiteral("torso"),
        0,
        0.01,
        0.05);

    QTRY_VERIFY_WITH_TIMEOUT(
        bridge.hasCommand(QStringLiteral("joint_nudge")),
        1000);
    bridge.takeCommand(QStringLiteral("joint_nudge"));
    QVERIFY(!bridge.hasCommand(QStringLiteral("stop")));
    QCOMPARE(
        stateSpy.last().at(0).toString(),
        QStringLiteral("JointBusy"));

    bridge.reply(QJsonObject{{QStringLiteral("success"), true}});
    QTRY_VERIFY_WITH_TIMEOUT(
        stateSpy.last().at(0).toString() == QStringLiteral("Ready"),
        1000);

    bridge.reply(
        statusResponse(
            ComponentState::On,
            ComponentState::On,
            ComponentState::On,
            true,
            true));
    QTest::qWait(50);
    QCOMPARE(
        stateSpy.last().at(0).toString(),
        QStringLiteral("Ready"));
}

void TestSystemStatus::longMotionKeepsCanonicalComponentStatusDuringBridgeSilence()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    RobotController controller;
    QSignalSpy stateSpy(&controller, &RobotController::stateChanged);

    connectAndReplyInitialStatus(
        controller,
        bridge,
        statusResponse(
            ComponentState::On,
            ComponentState::On,
            ComponentState::On,
            true,
            true));
    QTRY_VERIFY_WITH_TIMEOUT(
        !stateSpy.isEmpty()
            && stateSpy.last().at(0).toString()
                == QStringLiteral("Ready"),
        1000);

    controller.nudgeJoint(
        QStringLiteral("torso"),
        0,
        0.01,
        5.0);

    QTRY_VERIFY_WITH_TIMEOUT(
        bridge.hasCommand(QStringLiteral("joint_nudge")),
        1000);
    bridge.takeCommand(QStringLiteral("joint_nudge"));

    // Simulate a bridge that executes the five-second Robot API call
    // synchronously and cannot answer status polling until it completes.
    QTest::qWait(5200);

    QCOMPARE(
        controller.systemConfiguration().power.state,
        ComponentState::On);
    QCOMPARE(
        controller.systemConfiguration().servo.state,
        ComponentState::On);
    QCOMPARE(
        controller.systemConfiguration().stream.state,
        ComponentState::On);
    QCOMPARE(
        stateSpy.last().at(0).toString(),
        QStringLiteral("JointBusy"));
}

void TestSystemStatus::largeJointMoveUsesOneTotalDurationAndKeepsStreamOn()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    RobotController controller;
    QSignalSpy stateSpy(&controller, &RobotController::stateChanged);

    connectAndReplyInitialStatus(
        controller,
        bridge,
        statusResponse(
            ComponentState::On,
            ComponentState::On,
            ComponentState::On,
            true,
            true));
    QTRY_VERIFY_WITH_TIMEOUT(
        !stateSpy.isEmpty()
            && stateSpy.last().at(0).toString()
                == QStringLiteral("Ready"),
        1000);

    constexpr double requestedDelta =
        179.0 * 3.14159265358979323846 / 180.0;
    constexpr double requestedTotalTime = 5.0;
    controller.nudgeJoint(
        QStringLiteral("head"),
        0,
        requestedDelta,
        requestedTotalTime);

    double sentDelta = 0.0;
    double sentMinimumTime = 0.0;
    int segmentCount = 0;

    while (sentDelta + 1e-9 < requestedDelta)
    {
        QTRY_VERIFY_WITH_TIMEOUT(
            bridge.hasCommand(QStringLiteral("joint_nudge")),
            1000);
        const QJsonObject command =
            bridge.takeCommand(QStringLiteral("joint_nudge"));

        const double segmentDelta =
            command.value(QStringLiteral("delta")).toDouble();
        const double segmentTime =
            command.value(QStringLiteral("minimum_time")).toDouble();

        QVERIFY(segmentDelta > 0.0);
        QVERIFY(segmentDelta <= 0.20 + 1e-9);
        QVERIFY(segmentTime >= 0.20);
        QVERIFY(segmentTime < requestedTotalTime);

        sentDelta += segmentDelta;
        sentMinimumTime += segmentTime;
        ++segmentCount;
        QVERIFY(segmentCount < 32);

        bridge.reply(
            QJsonObject{
                {QStringLiteral("success"), true},
                {QStringLiteral("command"), QStringLiteral("joint_nudge")}
            });
    }

    QCOMPARE(segmentCount, 16);
    QVERIFY(qAbs(sentDelta - requestedDelta) < 1e-9);
    QVERIFY(qAbs(sentMinimumTime - requestedTotalTime) < 1e-9);
    QTRY_VERIFY_WITH_TIMEOUT(
        stateSpy.last().at(0).toString() == QStringLiteral("Ready"),
        1000);
    QCOMPARE(
        controller.systemConfiguration().stream.state,
        ComponentState::On);
}

void TestSystemStatus::jointBusyIgnoresStaleResponseAndRecoversOnTimeout()
{
    RobotController controller;
    JointBusyState state(QStringLiteral("Joint nudge"), 42);
    const QJsonObject success{{QStringLiteral("success"), true}};

    QVERIFY(!state.onResponse(
        controller,
        41,
        QStringLiteral("Joint nudge"),
        success));
    QVERIFY(!state.onRequestTimeout(
        controller,
        41,
        QStringLiteral("Joint nudge")));

    std::unique_ptr<RobotState> recovered = state.onRequestTimeout(
        controller,
        42,
        QStringLiteral("Joint nudge"));
    QVERIFY(recovered);
    QCOMPARE(recovered->name(), QStringLiteral("Ready"));
}

void TestSystemStatus::disconnectBecomesUnknownAndReconnectResynchronizes()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    RobotController controller;

    connectAndReplyInitialStatus(
        controller,
        bridge,
        statusResponse(
            ComponentState::On,
            ComponentState::On,
            ComponentState::On));
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.systemConfiguration().stream.state == ComponentState::On,
        1000);

    controller.disconnectFromBridge();
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.systemConfiguration().power.state
            == ComponentState::Unknown,
        1000);
    QCOMPARE(
        controller.systemConfiguration().servo.state,
        ComponentState::Unknown);
    QCOMPARE(
        controller.systemConfiguration().stream.state,
        ComponentState::Unknown);

    bridge.clearCommands();
    controller.connectToBridge(QStringLiteral("127.0.0.1"), bridge.port());
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("status")), 1000);
    bridge.takeCommand(QStringLiteral("status"));
    bridge.reply(
        statusResponse(
            ComponentState::Off,
            ComponentState::Off,
            ComponentState::Off));

    QTRY_VERIFY_WITH_TIMEOUT(
        controller.systemConfiguration().power.state == ComponentState::Off,
        1000);
}

void TestSystemStatus::externalClientChangesAreReflectedByPolling()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    RobotController controller;

    connectAndReplyInitialStatus(
        controller,
        bridge,
        statusResponse(
            ComponentState::On,
            ComponentState::On,
            ComponentState::On));
    QTRY_VERIFY_WITH_TIMEOUT(
        controller.systemConfiguration().stream.state == ComponentState::On,
        1000);

    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("status")), 1200);
    bridge.takeCommand(QStringLiteral("status"));
    bridge.reply(
        statusResponse(
            ComponentState::Off,
            ComponentState::On,
            ComponentState::On));

    QTRY_VERIFY_WITH_TIMEOUT(
        controller.systemConfiguration().power.state == ComponentState::Off,
        1000);
    QCOMPARE(
        controller.systemConfiguration().servo.state,
        ComponentState::On);
    QCOMPARE(
        controller.systemConfiguration().stream.state,
        ComponentState::On);
}

void TestSystemStatus::jointTimerPollsDuringMotionAndRestartsOnReconnect()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    RobotController controller;
    QSignalSpy jointsSpy(&controller, &RobotController::jointStatusReceived);
    QSignalSpy stateSpy(&controller, &RobotController::stateChanged);
    connectAndReplyInitialStatus(
        controller, bridge,
        statusResponse(ComponentState::On, ComponentState::On,
                       ComponentState::On, true, true));

    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("joints_status")), 1000);
    bridge.takeCommand(QStringLiteral("joints_status"));
    // No accumulating requests even when the bridge is slow.
    controller.refreshJoints();
    QTest::qWait(1100);
    QVERIFY(!bridge.hasCommand(QStringLiteral("joints_status")));

    const QJsonObject snapshot{
        {QStringLiteral("success"), true},
        {QStringLiteral("groups"), QJsonObject{
            {QStringLiteral("head"), QJsonArray{0.1, 0.2}}}}
    };
    bridge.reply(snapshot);
    QTRY_COMPARE_WITH_TIMEOUT(jointsSpy.count(), 1, 1000);
    controller.nudgeJoint(QStringLiteral("head"), 0, 0.01, 5.0);
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("joint_nudge")), 1000);
    bridge.takeCommand(QStringLiteral("joint_nudge"));

    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("joints_status")), 1000);
    bridge.takeCommand(QStringLiteral("joints_status"));
    bridge.reply(snapshot);
    QTRY_COMPARE_WITH_TIMEOUT(jointsSpy.count(), 2, 1000);
    QCOMPARE(stateSpy.last().at(0).toString(), QStringLiteral("JointBusy"));

    controller.disconnectFromBridge();
    bridge.clearCommands();
    QTest::qWait(600);
    QVERIFY(!bridge.hasCommand(QStringLiteral("joints_status")));
    controller.connectToBridge(QStringLiteral("127.0.0.1"), bridge.port());
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("joints_status")), 1000);
    bridge.takeCommand(QStringLiteral("joints_status"));
    bridge.reply(snapshot);
    QTRY_COMPARE_WITH_TIMEOUT(jointsSpy.count(), 3, 1000);
}

void TestSystemStatus::jointTimerRetriesTimeoutAndIgnoresLateSnapshot()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    RobotController controller;
    QSignalSpy jointsSpy(&controller, &RobotController::jointStatusReceived);
    connectAndReplyInitialStatus(
        controller, bridge,
        statusResponse(ComponentState::On, ComponentState::On, ComponentState::On));
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("joints_status")), 1000);
    bridge.takeCommand(QStringLiteral("joints_status"));
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("joints_status")), 4200);
    bridge.takeCommand(QStringLiteral("joints_status"));

    bridge.reply(QJsonObject{{QStringLiteral("head"), QJsonArray{0.1, 0.2}}});
    QTest::qWait(50);
    QCOMPARE(jointsSpy.count(), 0);
    const QJsonObject fresh{{QStringLiteral("head"), QJsonArray{0.3, 0.4}}};
    bridge.reply(fresh);
    QTRY_COMPARE_WITH_TIMEOUT(jointsSpy.count(), 1, 1000);
    QCOMPARE(jointsSpy.last().at(0).toJsonObject(), fresh);
}

void TestSystemStatus::continuousJointDisplayDoesNotInterruptDraggingOrSendCommands()
{
    MainWindow window;
    auto *controller = window.findChild<RobotController *>();
    auto *label = window.findChild<QLineEdit *>(QStringLiteral("headJoint0Value"));
    auto *slider = window.findChild<QSlider *>(QStringLiteral("headJoint0Slider"));
    QVERIFY(controller);
    QVERIFY(label);
    QVERIFY(slider);
    QSignalSpy logSpy(controller, &RobotController::logMessage);
    QSignalSpy changedSpy(slider, &QSlider::valueChanged);

    controller->jointStatusReceived(
        QJsonObject{{QStringLiteral("head"), QJsonArray{0.1, 0.2}}});
    QCOMPARE(label->text(), QStringLiteral("5.73"));
    QCOMPARE(changedSpy.count(), 0);

    slider->setSliderDown(true);
    slider->setValue(4500);
    controller->jointStatusReceived(
        QJsonObject{{QStringLiteral("head"), QJsonArray{0.2, 0.3}}});
    QCOMPARE(label->text(), QStringLiteral("11.46"));
    QCOMPARE(slider->value(), 4500);
    QVERIFY(qAbs(slider->property("confirmedDegrees").toDouble() - 11.4591559) < 1e-6);
    // End the artificial drag without generating a user release command.
    {
        const QSignalBlocker blocker(slider);
        slider->setSliderDown(false);
    }
    controller->jointStatusReceived(
        QJsonObject{{QStringLiteral("head"), QJsonArray{QJsonValue::Null, 0.4}}});
    QCOMPARE(label->text(), QStringLiteral("11.46"));
    QCOMPARE(logSpy.count(), 0);
}

void TestSystemStatus::compactLayoutKeepsAllJointRowsVisible()
{
    MainWindow window;
    auto *controller = window.findChild<RobotController *>();
    auto *scrollArea = window.findChild<QScrollArea *>(QStringLiteral("jointScrollArea"));
    QVERIFY(controller);
    QVERIFY(scrollArea);

    // Render a confirmed-ready view without connecting to real hardware.
    const ComponentViewState on{
        ComponentState::On, ComponentState::On, QStringLiteral("robot_api"), false};
    controller->systemConfigurationChanged(SystemConfigurationView{on, on, on});
    controller->stateChanged(QStringLiteral("Ready"), true, true, true, true, false);
    controller->jointStatusReceived(QJsonObject{
        {QStringLiteral("torso"), QJsonArray{0.0, 0.1, -0.2, 0.1, 0.0, 0.0}},
        {QStringLiteral("head"), QJsonArray{0.1, 0.2}},
        {QStringLiteral("right_arm"), QJsonArray{0.1, -0.2, 0.3, -0.4, 0.5, 0.0, -0.1}},
        {QStringLiteral("left_arm"), QJsonArray{0.1, 0.2, -0.3, -0.4, 0.5, 0.0, 0.1}}
    });
    window.resize(1200, 840);
    window.show();
    QTest::qWait(100);

    QCOMPARE(window.width(), 1200);
    for (QPushButton *button : window.findChildren<QPushButton *>())
    {
        QVERIFY(button->text().compare(QStringLiteral("CANCEL"), Qt::CaseInsensitive) != 0);
    }
    QCOMPARE(scrollArea->verticalScrollBar()->maximum(), 0);
    QCOMPARE(scrollArea->horizontalScrollBar()->maximum(), 0);
    auto *torso = window.findChild<QGroupBox *>(QStringLiteral("torsoJointGroup"));
    auto *head = window.findChild<QGroupBox *>(QStringLiteral("headJointGroup"));
    auto *rightArm = window.findChild<QGroupBox *>(QStringLiteral("right_armJointGroup"));
    auto *leftArm = window.findChild<QGroupBox *>(QStringLiteral("left_armJointGroup"));
    QVERIFY(torso);
    QVERIFY(head);
    QVERIFY(rightArm);
    QVERIFY(leftArm);
    QCOMPARE(torso->y(), head->y());
    QCOMPARE(torso->height(), head->height());
    QCOMPARE(rightArm->y(), leftArm->y());
    QCOMPARE(rightArm->height(), leftArm->height());
    QCOMPARE(torso->x(), rightArm->x());
    QCOMPARE(head->x(), leftArm->x());
    QVERIFY(qAbs(torso->width() - head->width()) <= 1);
    int valueCount = 0;
    for (QLabel *label : window.findChildren<QLabel *>())
    {
        QVERIFY(!label->text().startsWith(QStringLiteral("State: ")));
        QVERIFY(!label->text().startsWith(QStringLiteral("Kết nối: ")));
    }
    for (QLineEdit *label : window.findChildren<QLineEdit *>())
    {
        if (!label->objectName().endsWith(QStringLiteral("Value"))
            || !label->objectName().contains(QStringLiteral("Joint")))
        {
            continue;
        }
        ++valueCount;
        const QRect viewportRect(
            label->mapTo(scrollArea->viewport(), QPoint(0, 0)), label->size());
        QVERIFY(scrollArea->viewport()->rect().contains(viewportRect));
        QVERIFY(label->width() >= 92);
    }
    QCOMPARE(valueCount, 22);
    QVERIFY(window.grab().save(QStringLiteral("joint-layout-1200x840.png")));

    window.resize(1200, 700);
    QTest::qWait(100);
    QCOMPARE(scrollArea->horizontalScrollBar()->maximum(), 0);
    QVERIFY(window.grab().save(QStringLiteral("joint-layout-1200x700.png")));
}

void TestSystemStatus::jointAngleEntrySubmitsOnlyOnEnterAndReportsFailures()
{
    FakeBridge bridge;
    QVERIFY(bridge.listen());
    MainWindow window;
    auto *controller = window.findChild<RobotController *>();
    auto *editor = window.findChild<QLineEdit *>(QStringLiteral("headJoint0Value"));
    auto *other = window.findChild<QLineEdit *>(QStringLiteral("headJoint1Value"));
    QVERIFY(controller);
    QVERIFY(editor);
    QVERIFY(other);
    QSignalSpy failureSpy(controller, &RobotController::jointMotionFailed);
    QSignalSpy stateSpy(controller, &RobotController::stateChanged);
    connectAndReplyInitialStatus(*controller, bridge,
        statusResponse(ComponentState::On, ComponentState::On, ComponentState::On, true, true));
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("joints_status")), 1000);
    bridge.takeCommand(QStringLiteral("joints_status"));
    bridge.reply(QJsonObject{{QStringLiteral("head"), QJsonArray{0.1, 0.0}}});
    QTRY_COMPARE_WITH_TIMEOUT(editor->text(), QStringLiteral("5.73"), 1000);
    window.show();
    window.activateWindow();
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    editor->selectAll();
    QTest::keyClicks(editor, "20");
    controller->jointStatusReceived(QJsonObject{{QStringLiteral("head"), QJsonArray{0.2, 0.0}}});
    QCOMPARE(editor->text(), QStringLiteral("20"));
    QTest::qWait(600);
    QVERIFY(!bridge.hasCommand(QStringLiteral("joint_nudge")));
    other->setFocus();
    QTRY_COMPARE(editor->text(), QStringLiteral("11.46"));
    QVERIFY(!bridge.hasCommand(QStringLiteral("joint_nudge")));

    editor->setFocus();
    editor->selectAll();
    QTest::keyClicks(editor, "20");
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("joint_nudge")), 1000);
    const QJsonObject command = bridge.takeCommand(QStringLiteral("joint_nudge"));
    QCOMPARE(command.value(QStringLiteral("group")).toString(), QStringLiteral("head"));
    QCOMPARE(command.value(QStringLiteral("joint_index")).toInt(), 0);
    QVERIFY(qAbs(command.value(QStringLiteral("delta")).toDouble()
        - (20.0 * 3.14159265358979323846 / 180.0 - 0.2)) < 1e-9);
    QCOMPARE(editor->text(), QStringLiteral("11.46"));
    bridge.reply(QJsonObject{{QStringLiteral("success"), true},
        {QStringLiteral("command"), QStringLiteral("joint_nudge")}});
    QTRY_COMPARE_WITH_TIMEOUT(stateSpy.last().at(0).toString(), QStringLiteral("Ready"), 1000);
    QCOMPARE(failureSpy.count(), 0);
    // An ACK alone cannot place the displayed position at the entered target.
    QCOMPARE(editor->text(), QStringLiteral("11.46"));
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("joints_status")), 1000);
    bridge.takeCommand(QStringLiteral("joints_status"));
    bridge.reply(QJsonObject{{QStringLiteral("head"),
        QJsonArray{20.0 * 3.14159265358979323846 / 180.0, 0.0}}});
    QTRY_COMPARE_WITH_TIMEOUT(editor->text(), QStringLiteral("20.00"), 1000);

    editor->setFocus();
    editor->selectAll();
    QTest::keyClicks(editor, "21");
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(bridge.hasCommand(QStringLiteral("joint_nudge")), 1000);
    bridge.takeCommand(QStringLiteral("joint_nudge"));
    bridge.reply(QJsonObject{{QStringLiteral("success"), false},
        {QStringLiteral("command"), QStringLiteral("joint_nudge")},
        {QStringLiteral("message"), QStringLiteral("joint limit exceeded")}});
    QTRY_COMPARE_WITH_TIMEOUT(failureSpy.count(), 1, 1000);
    auto *dialog = window.findChild<QMessageBox *>(QStringLiteral("jointMotionErrorDialog"));
    QVERIFY(dialog);
    QCOMPARE(dialog->text(), QStringLiteral("joint limit exceeded"));
    QCOMPARE(editor->text(), QStringLiteral("20.00"));
    QVERIFY(!bridge.hasCommand(QStringLiteral("joint_nudge")));
    dialog->done(QMessageBox::Ok);
}

void TestSystemStatus::jointAngleEntryRejectsInvalidTargetsAndTimeoutReportsFailure()
{
    MainWindow window;
    auto *controller = window.findChild<RobotController *>();
    auto *editor = window.findChild<QLineEdit *>(QStringLiteral("headJoint0Value"));
    QVERIFY(controller);
    QVERIFY(editor);
    controller->stateChanged(QStringLiteral("Ready"), true, true, true, true, false);
    controller->jointStatusReceived(QJsonObject{{QStringLiteral("head"), QJsonArray{0.1, 0.0}}});
    window.show();
    window.activateWindow();
    for (const QString &draft : {QStringLiteral("abc"), QStringLiteral("180")})
    {
        editor->setFocus();
        editor->setText(draft);
        QTest::keyClick(editor, Qt::Key_Return);
        auto *dialog = window.findChild<QMessageBox *>(QStringLiteral("jointMotionErrorDialog"));
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        QVERIFY(!dialog->text().isEmpty());
        dialog->done(QMessageBox::Ok);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QSignalSpy failureSpy(controller, &RobotController::jointMotionFailed);
    JointBusyState busy(QStringLiteral("Joint nudge"), 42);
    QVERIFY(!busy.onRequestTimeout(*controller, 41, QStringLiteral("Joint nudge")));
    QCOMPARE(failureSpy.count(), 0);
    QVERIFY(busy.onRequestTimeout(*controller, 42, QStringLiteral("Joint nudge")));
    QCOMPARE(failureSpy.count(), 1);
    QVERIFY(window.findChild<QMessageBox *>(QStringLiteral("jointMotionErrorDialog")));
}

QTEST_MAIN(TestSystemStatus)
#include "TestSystemStatus.moc"
