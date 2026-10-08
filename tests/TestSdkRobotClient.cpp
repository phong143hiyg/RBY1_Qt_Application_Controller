#include "sdk/SdkRobotClient.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QtMath>
#include <QTimer>
#include <QUrl>

class TestSdkRobotClient final : public QObject
{
    Q_OBJECT
private slots:
    void disconnectedOperationsDoNotSendCommands()
    {
        SdkRobotClient client;
        QSignalSpy errors(&client, &IRby1Client::clientError);
        QSignalSpy responses(&client, &IRby1Client::responseReceived);
        QVERIFY(!client.isConnected());
        QCOMPARE(client.readStatus(100), quint64{0});
        QCOMPARE(client.readJoints(100), quint64{0});
        QCOMPARE(client.moveJointTo(QStringLiteral("head"), 0, 0.1, 100), quint64{0});
        QCOMPARE(client.setComponent(RobotComponent::Power, true, QStringLiteral("Power ON"), 100), quint64{0});
        QCOMPARE(client.executePose(QStringLiteral("zero_pose"), QStringLiteral("Zero"), 100), quint64{0});
        QCOMPARE(client.executeSimple(QStringLiteral("cancel"), QStringLiteral("Cancel"), 100), quint64{0});
        QCOMPARE(client.setVelocity(0.1, 0.0, 0.0), quint64{0});
        QCOMPARE(errors.count(), 6);
        QCOMPARE(responses.count(), 0);
    }

    void failedConnectionKeepsEventLoopResponsive()
    {
        SdkRobotClient client;
        QSignalSpy errors(&client, &IRby1Client::clientError);
        QSignalSpy connected(&client, &IRby1Client::robotConnected);
        int ticks = 0;
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&ticks] { ++ticks; });
        timer.start(10);
        // Closed local port: never connect this test to a real robot.
        client.connectToRobot(QStringLiteral("127.0.0.1"), 1, Rby1Model::M);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 3000);
        QVERIFY(ticks > 5);
        QVERIFY(!client.isConnected());
        QCOMPARE(connected.count(), 0);
        const QString message = errors.first().first().toString();
        QVERIFY2(message.contains(QStringLiteral("127.0.0.1:1")), qPrintable(message));
        QVERIFY2(message.contains(QStringLiteral("simulator/server")), qPrintable(message));
    }

    void disconnectCancelsPendingConnection()
    {
        SdkRobotClient client;
        QSignalSpy connected(&client, &IRby1Client::robotConnected);
        client.connectToRobot(QStringLiteral("127.0.0.1"), 1, Rby1Model::M);
        client.disconnectFromRobot();
        QTest::qWait(1100);
        QVERIFY(!client.isConnected());
        QCOMPARE(connected.count(), 0);
    }

    void connectsToConfiguredRobotReadOnly()
    {
        const QString endpoint = qEnvironmentVariable("RBY1_TEST_ROBOT_ADDRESS");
        if (endpoint.isEmpty())
            QSKIP("Set RBY1_TEST_ROBOT_ADDRESS to run the read-only SDK integration test.");

        const QUrl address(QStringLiteral("tcp://") + endpoint);
        QVERIFY(!address.host().isEmpty());
        QVERIFY(address.port() > 0 && address.port() <= 65535);

        SdkRobotClient client;
        QSignalSpy connected(&client, &IRby1Client::robotConnected);
        QSignalSpy errors(&client, &IRby1Client::clientError);
        QSignalSpy responses(&client, &IRby1Client::responseReceived);
        const Rby1Model model = qEnvironmentVariable("RBY1_TEST_ROBOT_MODEL", QStringLiteral("m"))
                                    .trimmed().compare(QStringLiteral("a"), Qt::CaseInsensitive) == 0
            ? Rby1Model::A : Rby1Model::M;
        client.connectToRobot(address.host(), static_cast<quint16>(address.port()), model);
        QTRY_VERIFY_WITH_TIMEOUT(!connected.isEmpty() || !errors.isEmpty(), 8000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        QVERIFY(client.isConnected());

        QVERIFY(client.readJoints(2000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 4000);
        QVERIFY(errors.isEmpty());
        const QJsonObject response = responses.first().at(2).toJsonObject();
        QVERIFY(response.value(QStringLiteral("success")).toBool());
        const QJsonObject groups = response.value(QStringLiteral("groups")).toObject();
        QCOMPARE(groups.value(QStringLiteral("torso")).toObject().value(QStringLiteral("positions")).toArray().size(), 6);
        QCOMPARE(groups.value(QStringLiteral("right_arm")).toObject().value(QStringLiteral("positions")).toArray().size(), 7);
        QCOMPARE(groups.value(QStringLiteral("left_arm")).toObject().value(QStringLiteral("positions")).toArray().size(), 7);
        QCOMPARE(groups.value(QStringLiteral("head")).toObject().value(QStringLiteral("positions")).toArray().size(), 2);

        QVERIFY(client.readStatus(2000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(responses.size() >= 2 || !errors.isEmpty(), 4000);
        QVERIFY(errors.isEmpty());
        QVERIFY(responses.at(1).at(2).toJsonObject().value(QStringLiteral("success")).toBool());
    }

    void enablesControlManagerOnConfiguredSimulator()
    {
        if (qEnvironmentVariableIntValue("RBY1_TEST_ENABLE_MOTION") != 1)
            QSKIP("Set RBY1_TEST_ENABLE_MOTION=1 only for an isolated simulator motion test.");

        const QUrl address(QStringLiteral("tcp://")
                           + qEnvironmentVariable("RBY1_TEST_ROBOT_ADDRESS",
                                                  QStringLiteral("127.0.0.1:50051")));
        QVERIFY(!address.host().isEmpty());
        QVERIFY(address.port() > 0 && address.port() <= 65535);

        SdkRobotClient client;
        QSignalSpy connected(&client, &IRby1Client::robotConnected);
        QSignalSpy errors(&client, &IRby1Client::clientError);
        QSignalSpy responses(&client, &IRby1Client::responseReceived);
        client.connectToRobot(address.host(), static_cast<quint16>(address.port()), Rby1Model::M);
        QTRY_VERIFY_WITH_TIMEOUT(!connected.isEmpty() || !errors.isEmpty(), 8000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));

        QVERIFY(client.setComponent(RobotComponent::Power, true,
                                    QStringLiteral("Power ON"), 5000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 7000);
        QVERIFY(errors.isEmpty());
        QJsonObject componentResponse = responses.takeFirst().at(2).toJsonObject();
        QVERIFY2(componentResponse.value(QStringLiteral("success")).toBool(),
                 qPrintable(componentResponse.value(QStringLiteral("message")).toString()));

        QVERIFY(client.setComponent(RobotComponent::Servo, true,
                                    QStringLiteral("Servo ON"), 5000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 7000);
        QVERIFY(errors.isEmpty());
        componentResponse = responses.takeFirst().at(2).toJsonObject();
        QVERIFY2(componentResponse.value(QStringLiteral("success")).toBool(),
                 qPrintable(componentResponse.value(QStringLiteral("message")).toString()));

        const quint64 enableId = client.setComponent(
            RobotComponent::Stream, true, QStringLiteral("Control Manager ON"), 5000);
        QVERIFY(enableId != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 7000);
        QVERIFY(errors.isEmpty());
        const QJsonObject enableResponse = responses.takeFirst().at(2).toJsonObject();
        QVERIFY2(enableResponse.value(QStringLiteral("success")).toBool(),
                 qPrintable(enableResponse.value(QStringLiteral("message")).toString()));

        QVERIFY(client.readStatus(3000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 5000);
        QVERIFY(errors.isEmpty());
        const QJsonObject statusResponse = responses.takeFirst().at(2).toJsonObject();
        const QJsonObject stream = statusResponse.value(QStringLiteral("components"))
                                       .toObject().value(QStringLiteral("stream")).toObject();
        QVERIFY(stream.value(QStringLiteral("known")).toBool());
        QVERIFY(stream.value(QStringLiteral("enabled")).toBool());
        QCOMPARE(statusResponse.value(QStringLiteral("status")).toObject()
                     .value(QStringLiteral("state")).toString(), QStringLiteral("SDK connected"));

        // Regression for the torso tracking faults: reproduce the manual upper
        // targets shown in the UI before returning the complete robot to zero.
        QVERIFY(client.moveJointTo(QStringLiteral("torso"), 3,
                                   qDegreesToRadians(89.0), 12000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 12000);
        QVERIFY(errors.isEmpty());
        const QJsonObject torsoForward = responses.takeFirst().at(2).toJsonObject();
        QVERIFY2(torsoForward.value(QStringLiteral("success")).toBool(),
                 qPrintable(torsoForward.value(QStringLiteral("message")).toString()));

        QVERIFY(client.moveJointTo(QStringLiteral("torso"), 4,
                                   qDegreesToRadians(29.0), 12000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 12000);
        QVERIFY(errors.isEmpty());
        const QJsonObject torsoFifth = responses.takeFirst().at(2).toJsonObject();
        QVERIFY2(torsoFifth.value(QStringLiteral("success")).toBool(),
                 qPrintable(torsoFifth.value(QStringLiteral("message")).toString()));

        // The SDK rejects the URDF-only +/-135 degree interval because the
        // simulator's MuJoCo torso_5 joint physically stops at +/-90 degrees.
        QVERIFY(client.moveJointTo(QStringLiteral("torso"), 5,
                                   qDegreesToRadians(134.0), 12000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 4000);
        QVERIFY(errors.isEmpty());
        const QJsonObject rejectedTorsoSixth = responses.takeFirst().at(2).toJsonObject();
        QVERIFY(!rejectedTorsoSixth.value(QStringLiteral("success")).toBool());
        QVERIFY(rejectedTorsoSixth.value(QStringLiteral("message")).toString()
                    .contains(QStringLiteral("+/-90")));

        QVERIFY(client.moveJointTo(QStringLiteral("torso"), 5,
                                   qDegreesToRadians(89.0), 12000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 14000);
        QVERIFY(errors.isEmpty());
        const QJsonObject torsoSixth = responses.takeFirst().at(2).toJsonObject();
        QVERIFY2(torsoSixth.value(QStringLiteral("success")).toBool(),
                 qPrintable(torsoSixth.value(QStringLiteral("message")).toString()));

        QVERIFY(client.executePose(QStringLiteral("zero_pose"),
                                   QStringLiteral("Initial"), 20000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 22000);
        QVERIFY(errors.isEmpty());
        const QJsonObject zeroResponse = responses.takeFirst().at(2).toJsonObject();
        QVERIFY2(zeroResponse.value(QStringLiteral("success")).toBool(),
                 qPrintable(zeroResponse.value(QStringLiteral("message")).toString()));

        // Absolute targets execute as bounded position motions.
        QVERIFY(client.moveJointTo(QStringLiteral("head"), 0, 0.30, 8000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 7000);
        QVERIFY(errors.isEmpty());
        const QJsonObject forwardResponse = responses.takeFirst().at(2).toJsonObject();
        QVERIFY2(forwardResponse.value(QStringLiteral("success")).toBool(),
                 qPrintable(forwardResponse.value(QStringLiteral("message")).toString()));

        QVERIFY(client.moveJointTo(QStringLiteral("head"), 0, 0.0, 8000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 7000);
        QVERIFY(errors.isEmpty());
        const QJsonObject returnResponse = responses.takeFirst().at(2).toJsonObject();
        QVERIFY2(returnResponse.value(QStringLiteral("success")).toBool(),
                 qPrintable(returnResponse.value(QStringLiteral("message")).toString()));

        // A fresh SDK session has no user-defined ready pose. Arms Ready must
        // still execute by using the built-in RBY1 folded-arm preset.
        QVERIFY(client.executePose(QStringLiteral("arms_ready"),
                                   QStringLiteral("Co hai tay"), 7000) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!responses.isEmpty() || !errors.isEmpty(), 9000);
        QVERIFY(errors.isEmpty());
        const QJsonObject armsReadyResponse = responses.takeFirst().at(2).toJsonObject();
        QVERIFY2(armsReadyResponse.value(QStringLiteral("success")).toBool(),
                 qPrintable(armsReadyResponse.value(QStringLiteral("message")).toString()));
    }
};

QTEST_GUILESS_MAIN(TestSdkRobotClient)
#include "TestSdkRobotClient.moc"
