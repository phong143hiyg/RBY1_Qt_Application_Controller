#include "controller/RobotController.hpp"
#include "sdk/IRby1Client.hpp"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <algorithm>

class FakeRby1Client final : public IRby1Client
{
public:
    using IRby1Client::IRby1Client;

    bool connected{false};
    bool respondToStatus{true};
    bool failNextPose{false};
    int connectCount{0};
    int disconnectCount{0};
    int velocityCount{0};
    int stopCount{0};
    int jointMoveCount{0};
    quint64 lastStatusId{0};

    void connectToRobot(const QString &, quint16, Rby1Model) override
    {
        ++connectCount;
        connected = true;
        QTimer::singleShot(0, this, [this] { emit robotConnected(); });
    }

    void disconnectFromRobot() override
    {
        ++disconnectCount;
        const bool notify = connected;
        connected = false;
        if (notify)
            emit robotDisconnected();
    }

    bool isConnected() const override { return connected; }

    quint64 readStatus(int) override
    {
        const quint64 id = nextId_++;
        lastStatusId = id;
        if (respondToStatus) {
            QTimer::singleShot(0, this, [this, id] {
                const auto on = [](bool enabled) {
                    return QJsonObject{{"known", true}, {"enabled", enabled},
                                       {"pending", false}, {"source", "fake"}};
                };
                emit responseReceived(id, QStringLiteral("Status"), QJsonObject{
                    {"success", true}, {"connected", true},
                    {"components", QJsonObject{{"power", on(true)},
                                                {"servo", on(true)},
                                                {"stream", on(true)}}},
                    {"status", QJsonObject{{"ready", true}, {"state", "fake"}}}});
            });
        }
        return id;
    }

    quint64 readJoints(int) override
    {
        const quint64 id = nextId_++;
        QTimer::singleShot(0, this, [this, id] {
            const auto group = [](int count) {
                QJsonArray positions;
                for (int i = 0; i < count; ++i) positions.append(0.0);
                return QJsonObject{{"positions", positions}};
            };
            emit responseReceived(id, QStringLiteral("Joints status"), QJsonObject{
                {"success", true},
                {"groups", QJsonObject{{"torso", group(6)}, {"head", group(2)},
                                        {"right_arm", group(7)}, {"left_arm", group(7)}}}});
        });
        return id;
    }

    quint64 setComponent(RobotComponent, bool, const QString &operation, int) override
    {
        return completeSoon(operation);
    }

    quint64 moveJointTo(const QString &, int, double, int) override
    {
        ++jointMoveCount;
        return nextId_++; // Deliberately pending to exercise concurrency gating.
    }

    quint64 executePose(const QString &, const QString &operation, int) override
    {
        if (failNextPose) {
            failNextPose = false;
            const quint64 id = nextId_++;
            QTimer::singleShot(0, this, [this, id, operation] {
                emit responseReceived(id, operation, QJsonObject{
                    {"success", false},
                    {"message", "Control Manager tracking protection"}});
            });
            return id;
        }
        return completeSoon(operation);
    }

    quint64 executeSimple(const QString &action, const QString &operation, int) override
    {
        if (action == QStringLiteral("stop")) ++stopCount;
        return completeSoon(operation);
    }

    quint64 setVelocity(double, double, double) override
    {
        ++velocityCount;
        return completeSoon(QStringLiteral("Velocity"));
    }

private:
    quint64 completeSoon(const QString &operation)
    {
        const quint64 id = nextId_++;
        QTimer::singleShot(0, this, [this, id, operation] {
            emit responseReceived(id, operation, QJsonObject{{"success", true}});
        });
        return id;
    }

    quint64 nextId_{1};
};

class TestRobotController final : public QObject
{
    Q_OBJECT

private slots:
    void unknownOffOnParsing()
    {
        const auto component = [](bool known, bool enabled) {
            return QJsonObject{{"known", known}, {"enabled", enabled},
                               {"pending", false}, {"source", "test"}};
        };
        const ParsedSystemStatus parsed = parseSystemStatus(QJsonObject{
            {"success", true},
            {"components", QJsonObject{{"power", component(false, false)},
                                        {"servo", component(true, false)},
                                        {"stream", component(true, true)}}}});
        QCOMPARE(parsed.power.state, ComponentState::Unknown);
        QCOMPARE(parsed.servo.state, ComponentState::Off);
        QCOMPARE(parsed.stream.state, ComponentState::On);
    }

    void connectDisconnectReconnect()
    {
        auto *fake = new FakeRby1Client;
        RobotController controller(fake);
        QSignalSpy states(&controller, &RobotController::stateChanged);

        controller.connectToRobot(QStringLiteral("127.0.0.1"), 50051, Rby1Model::M);
        QTRY_VERIFY_WITH_TIMEOUT(!states.isEmpty(), 500);
        controller.disconnectFromRobot();
        QCOMPARE(fake->disconnectCount, 1);
        controller.connectToRobot(QStringLiteral("127.0.0.1"), 50051, Rby1Model::M);
        QTRY_COMPARE_WITH_TIMEOUT(fake->connectCount, 2, 500);
    }

    void staleStateBlocksMotion()
    {
        auto *fake = new FakeRby1Client;
        RobotController controller(fake);
        QSignalSpy states(&controller, &RobotController::stateChanged);
        controller.connectToRobot(QStringLiteral("127.0.0.1"), 50051, Rby1Model::M);
        QTRY_VERIFY_WITH_TIMEOUT(!states.isEmpty()
            && states.last().at(0).toString() == QStringLiteral("Ready"), 1000);

        fake->respondToStatus = false;
        QTRY_VERIFY_WITH_TIMEOUT(states.last().at(0).toString()
            == QStringLiteral("Connected"), 3500);
        const int before = fake->velocityCount;
        controller.startDrive(0.1, 0.0, 0.0);
        QCOMPARE(fake->velocityCount, before);
    }

    void velocityValidationAndDeadManStop()
    {
        auto *fake = new FakeRby1Client;
        RobotController controller(fake);
        QSignalSpy states(&controller, &RobotController::stateChanged);
        controller.connectToRobot(QStringLiteral("127.0.0.1"), 50051, Rby1Model::M);
        QTRY_VERIFY_WITH_TIMEOUT(!states.isEmpty()
            && states.last().at(0).toString() == QStringLiteral("Ready"), 1000);

        controller.startDrive(10.0, 0.0, 0.0);
        QCOMPARE(fake->velocityCount, 0);
        controller.startDrive(0.1, 0.0, 0.0);
        QCOMPARE(fake->velocityCount, 1);
        controller.stopDrive();
        QCOMPARE(fake->stopCount, 1);
    }

    void jointValidationAndConcurrency()
    {
        auto *fake = new FakeRby1Client;
        RobotController controller(fake);
        QSignalSpy states(&controller, &RobotController::stateChanged);
        controller.connectToRobot(QStringLiteral("127.0.0.1"), 50051, Rby1Model::M);
        QTRY_VERIFY_WITH_TIMEOUT(!states.isEmpty()
            && states.last().at(0).toString() == QStringLiteral("Ready"), 1000);

        controller.moveJointTo(QStringLiteral("head"), 2, 0.1);
        QCOMPARE(fake->jointMoveCount, 0);
        controller.moveJointTo(QStringLiteral("head"), 0, 0.1);
        QCOMPARE(fake->jointMoveCount, 1);
        controller.moveJointTo(QStringLiteral("head"), 1, 0.1);
        QCOMPARE(fake->jointMoveCount, 1);
    }

    void failedPoseWaitsForCanonicalStatus()
    {
        auto *fake = new FakeRby1Client;
        RobotController controller(fake);
        QSignalSpy states(&controller, &RobotController::stateChanged);
        QSignalSpy logs(&controller, &RobotController::logMessage);
        controller.connectToRobot(QStringLiteral("127.0.0.1"), 50051, Rby1Model::M);
        QTRY_VERIFY_WITH_TIMEOUT(!states.isEmpty()
            && states.last().at(0).toString() == QStringLiteral("Ready"), 1000);

        fake->respondToStatus = false;
        fake->failNextPose = true;
        controller.sendPose(QStringLiteral("ready_pose"), QStringLiteral("Go Pose"));
        QTRY_VERIFY_WITH_TIMEOUT(states.last().at(0).toString()
            == QStringLiteral("Connected"), 1000);
        QVERIFY(std::any_of(logs.cbegin(), logs.cend(), [](const QList<QVariant> &entry) {
            return entry.first().toString().contains(QStringLiteral("connection remains active"));
        }));

        fake->respondToStatus = true;
        emit fake->requestTimedOut(fake->lastStatusId, QStringLiteral("Status"));
        QTRY_VERIFY_WITH_TIMEOUT(states.last().at(0).toString()
            == QStringLiteral("Ready"), 1000);
    }
};

QTEST_GUILESS_MAIN(TestRobotController)
#include "TestRobotController.moc"
