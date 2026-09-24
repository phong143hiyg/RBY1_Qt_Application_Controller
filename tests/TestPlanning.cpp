#include <QtTest>
#include <QTcpServer>
#include <QSignalSpy>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QLineEdit>
#include <QPushButton>
#include "planning/PlanningClient.hpp"
#include "planning/PlanningInput.hpp"
#include "ui/PlanningPanel.hpp"

namespace {
QJsonObject fixture() {
    QFile f(QStringLiteral(PLANNING_FIXTURE_PATH));
    if (!f.open(QIODevice::ReadOnly)) qFatal("Cannot read planning fixture");
    return QJsonDocument::fromJson(f.readAll()).object();
}
QJsonObject payload(const QString &command = "plan_pick_place") {
    return fixture().value("requests").toObject().value(command).toObject().value("payload").toObject();
}
QJsonObject planningEvent(const QString &id, int seq, const QString &status, const QString &command = "plan_pick_place") {
    QJsonObject e{{"protocol_version", 1}, {"type", "event"}, {"request_id", id}, {"command", command},
        {"event_seq", seq}, {"status", status}, {"stage", "test"}, {"payload", status == "succeeded" ? fixture().value("result") : QJsonValue(QJsonObject{})}};
    if (status == "failed") e.insert("error", QJsonObject{{"code", "PLANNING_FAILED"}, {"message", "test failure"}});
    return e;
}
class Server : public QObject {
public:
    QTcpServer listener;
    QTcpSocket *socket = nullptr;
    NdjsonParser parser;
    QVector<QJsonObject> requests;
    bool autoSync = true;
    QJsonObject snapshot;
    Server() {
        if (!listener.listen(QHostAddress::LocalHost, 0)) qFatal("Listen failed");
        connect(&listener, &QTcpServer::newConnection, this, [this] {
            socket = listener.nextPendingConnection(); parser.clear();
            connect(socket, &QTcpSocket::readyRead, this, [this] {
                for (const auto &f : parser.append(socket->readAll())) {
                    if (!f.valid) continue;
                    requests.append(f.object);
                    const auto cmd = f.object.value("command").toString();
                    if (autoSync && cmd == "get_capabilities") reply(f.object, fixture().value("capabilities").toObject());
                    if (autoSync && cmd == "get_scene") reply(f.object, fixture().value("scene").toObject());
                    if (autoSync && cmd == "get_task_status" && !snapshot.isEmpty()) reply(f.object, snapshot);
                }
            });
        });
    }
    void send(const QJsonObject &e) { socket->write(QJsonDocument(e).toJson(QJsonDocument::Compact) + '\n'); }
    void reply(const QJsonObject &r, const QJsonObject &p = {}, bool ok = true) {
        send({{"protocol_version", 1}, {"type", "response"}, {"request_id", r.value("request_id")},
            {"command", r.value("command")}, {"ok", ok}, {"payload", p}});
    }
    QJsonObject request(const QString &command) const {
        for (auto it = requests.crbegin(); it != requests.crend(); ++it)
            if (it->value("command") == command) return *it;
        return {};
    }
    int count(const QString &command) const {
        int n = 0; for (const auto &r : requests) n += r.value("command") == command; return n;
    }
    void open(PlanningClient &c) { c.connectToServer("127.0.0.1", listener.serverPort()); }
};
}

class TestPlanning : public QObject {
    Q_OBJECT
private slots:
    void correlationAndWrongEnvelopes();
    void ackSequenceAndOldTask();
    void terminalBeforeAck();
    void timeoutAndLateResponse();
    void cancelRaceAndConfirmation();
    void cancelAckTimeoutAndDisconnect();
    void reconnectRequiresStatus();
    void sceneModelAndValidationInvalidate();
    void capabilityGatingAndOldSceneResponse();
    void ndjsonLimitsAndFragments();
    void invalidInput();
    void panelUsesSceneAndRejectsNan();
    void mockIntegration_data();
    void mockIntegration();
};
void TestPlanning::correlationAndWrongEnvelopes() {
    Server s; PlanningClient c; s.open(c); QTRY_VERIFY(c.canStart());
    QSignalSpy spy(&c, &PlanningClient::responseReceived);
    const auto first = c.query("get_scene"), second = c.query("get_scene");
    QTRY_COMPARE(s.count("get_scene"), 3);
    auto req = s.request("get_scene"); QCOMPARE(req.value("request_id").toString(), second);
    QVERIFY(req.value("request_id").isString()); QCOMPARE(req.value("protocol_version").toInt(), 1);
    // These were automatically returned by the server: issue two manually correlated queries.
    spy.clear(); s.autoSync = false;
    const auto a = c.query("get_scene"), b = c.query("get_capabilities");
    QTRY_COMPARE(s.count("get_scene"), 4);
    auto ra = s.request("get_scene"), rb = s.request("get_capabilities");
    auto wrong = planningEvent("unknown", 1, "succeeded"); s.send(wrong);
    auto malformed = QJsonObject{{"protocol_version", 1}, {"type", "response"}, {"request_id", 42}, {"command", "get_scene"}, {"ok", true}};
    s.send(malformed); s.reply(QJsonObject{{"request_id", a}, {"command", "get_capabilities"}}, fixture().value("capabilities").toObject());
    s.reply(rb, fixture().value("capabilities").toObject()); s.reply(ra, fixture().value("scene").toObject());
    QTRY_COMPARE(spy.count(), 2); QCOMPARE(spy[0][0].toString(), b); QCOMPARE(spy[1][0].toString(), a);
    QVERIFY(first != second && a != b);
}
void TestPlanning::ackSequenceAndOldTask() {
    Server s; PlanningClient c; s.open(c); QTRY_VERIFY(c.canStart());
    QSignalSpy spy(&c, &PlanningClient::eventReceived);
    const auto id = c.plan("plan_pick_place", payload()); QVERIFY(!id.isEmpty());
    QVERIFY(c.plan("plan_to_pose", payload("plan_to_pose")).isEmpty());
    QTRY_COMPARE(s.count("plan_pick_place"), 1); s.reply(s.request("plan_pick_place"));
    QTest::qWait(20); QCOMPARE(c.state(), PlanningClient::State::Planning); QVERIFY(c.planId().isEmpty());
    s.send(planningEvent("wrong", 9, "succeeded")); s.send(planningEvent(id, 3, "planning"));
    s.send(planningEvent(id, 2, "failed")); s.send(planningEvent(id, 3, "failed"));
    QTRY_COMPARE(spy.count(), 1); QCOMPARE(c.state(), PlanningClient::State::Planning);
    s.send(planningEvent(id, 4, "succeeded")); QTRY_COMPARE(c.state(), PlanningClient::State::PlanReady);
    const auto newId = c.plan("plan_pick_place", payload()); QVERIFY(newId != id);
    s.send(planningEvent(id, 99, "failed")); QTest::qWait(20); QCOMPARE(c.state(), PlanningClient::State::Planning);
}
void TestPlanning::terminalBeforeAck() {
    Server s; PlanningClient c; s.open(c); QTRY_VERIFY(c.canStart());
    const auto id = c.plan("plan_pick_place", payload()); QTRY_COMPARE(s.count("plan_pick_place"), 1);
    s.send(planningEvent(id, 1, "succeeded")); QTRY_COMPARE(c.state(), PlanningClient::State::PlanReady);
    s.reply(s.request("plan_pick_place"), {}, false); s.send(planningEvent(id, 2, "failed"));
    QTest::qWait(30); QCOMPARE(c.state(), PlanningClient::State::PlanReady); QVERIFY(!c.planId().isEmpty());
}
void TestPlanning::timeoutAndLateResponse() {
    Server s; PlanningClient c; s.open(c); QTRY_VERIFY(c.canStart()); s.autoSync = false;
    QSignalSpy timeouts(&c, &PlanningClient::requestTimedOut), responses(&c, &PlanningClient::responseReceived);
    const auto query = c.query("get_scene", {}, 15); QTRY_COMPARE(timeouts.count(), 1);
    s.reply(s.request("get_scene"), fixture().value("scene").toObject()); QTest::qWait(20); QCOMPARE(responses.count(), 0);
    auto p = payload(); p.insert("planning_timeout_s", .01);
    const auto id = c.plan("plan_pick_place", p); QTRY_COMPARE(s.count("plan_pick_place"), 1); s.reply(s.request("plan_pick_place"));
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), PlanningClient::State::Failed, 2200);
    QVERIFY(c.busy()); QVERIFY(!c.canStart()); s.send(planningEvent(id, 3, "succeeded")); QTest::qWait(30);
    QCOMPARE(c.state(), PlanningClient::State::Failed); QVERIFY(c.planId().isEmpty());
    QVERIFY(!c.cancel().isEmpty()); QTRY_COMPARE(s.count("cancel_planning"), 1); s.reply(s.request("cancel_planning"));
    QTest::qWait(20); QCOMPARE(c.state(), PlanningClient::State::Cancelling);
    s.snapshot = planningEvent(id, 4, "cancelled"); s.autoSync = true; c.reconcile();
    QTRY_VERIFY(c.synchronized()); QCOMPARE(c.state(), PlanningClient::State::Cancelled); QVERIFY(!c.busy());
    QVERIFY(query != id);
}
void TestPlanning::cancelRaceAndConfirmation() {
    Server s; PlanningClient c; s.open(c); QTRY_VERIFY(c.canStart());
    auto id = c.plan("plan_pick_place", payload()); QTRY_COMPARE(s.count("plan_pick_place"), 1);
    const auto cancelId = c.cancel(); QVERIFY(!cancelId.isEmpty() && cancelId != id);
    QTRY_COMPARE(s.count("cancel_planning"), 1);
    QCOMPARE(s.request("cancel_planning").value("payload").toObject().value("target_request_id").toString(), id);
    s.reply(s.request("cancel_planning")); QTest::qWait(20); QCOMPARE(c.state(), PlanningClient::State::Cancelling);
    s.send(planningEvent(id, 1, "succeeded")); QTRY_COMPARE(c.state(), PlanningClient::State::Failed); QVERIFY(c.planId().isEmpty());
    id = c.plan("plan_pick_place", payload()); QVERIFY(!id.isEmpty()); c.cancel();
    s.send(planningEvent(id, 1, "cancelled")); QTRY_COMPARE(c.state(), PlanningClient::State::Cancelled);
    s.send(planningEvent(id, 2, "succeeded")); QTest::qWait(20); QCOMPARE(c.state(), PlanningClient::State::Cancelled);
}
void TestPlanning::reconnectRequiresStatus() {
    Server s; PlanningClient c; s.open(c); QTRY_VERIFY(c.canStart());
    const auto id = c.plan("plan_pick_place", payload()); QTRY_COMPARE(s.count("plan_pick_place"), 1);
    s.send(planningEvent(id, 2, "planning")); QTest::qWait(20); s.socket->abort();
    QTRY_COMPARE(c.state(), PlanningClient::State::Disconnected); QVERIFY(c.busy());
    s.autoSync = false; s.open(c); QTRY_COMPARE(s.count("get_capabilities"), 2);
    QVERIFY(!c.canStart()); QVERIFY(c.plan("plan_pick_place", payload()).isEmpty());
    s.reply(s.request("get_capabilities"), fixture().value("capabilities").toObject());
    QTRY_COMPARE(s.count("get_task_status"), 1); QCOMPARE(s.count("get_scene"), 1);
    s.reply(s.request("get_task_status"), planningEvent(id, 1, "succeeded")); QTest::qWait(20);
    QVERIFY(!c.synchronized()); QCOMPARE(s.count("get_scene"), 1);
    s.autoSync = true; s.snapshot = planningEvent(id, 3, "succeeded"); c.reconcile();
    QTRY_VERIFY(c.synchronized()); QCOMPARE(c.state(), PlanningClient::State::PlanReady); QCOMPARE(s.count("plan_pick_place"), 1);
    c.disconnectFromServer(); QTRY_COMPARE(c.state(), PlanningClient::State::Disconnected);
    s.open(c); QTRY_VERIFY(c.synchronized()); QCOMPARE(c.state(), PlanningClient::State::PlanReady);
}
void TestPlanning::cancelAckTimeoutAndDisconnect() {
    Server s; PlanningClient c; s.open(c); QTRY_VERIFY(c.canStart());
    auto p = payload(); p.insert("planning_timeout_s", 10);
    const auto id = c.plan("plan_pick_place", p); QTRY_COMPARE(s.count("plan_pick_place"), 1);
    s.reply(s.request("plan_pick_place"));
    QSignalSpy timeout(&c, &PlanningClient::requestTimedOut);
    const auto cancelId = c.cancel(); QTRY_COMPARE(s.count("cancel_planning"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(timeout.count(), 1, 3600);
    QCOMPARE(timeout[0][0].toString(), cancelId); QCOMPARE(c.state(), PlanningClient::State::Cancelling);
    QVERIFY(c.busy()); QVERIFY(!c.canStart());
    s.reply(s.request("cancel_planning")); s.socket->abort();
    QTRY_COMPARE(c.state(), PlanningClient::State::Disconnected);
    s.snapshot = planningEvent(id, 1, "planning"); s.open(c); QTRY_VERIFY(c.synchronized());
    QCOMPARE(c.state(), PlanningClient::State::Cancelling); QVERIFY(!c.canStart());
    s.send(planningEvent(id, 2, "cancelled")); QTRY_COMPARE(c.state(), PlanningClient::State::Cancelled);
    QVERIFY(c.planId().isEmpty());
}
void TestPlanning::sceneModelAndValidationInvalidate() {
    Server s; PlanningClient c; s.open(c); QTRY_VERIFY(c.canStart());
    auto id = c.plan("plan_pick_place", payload()); s.send(planningEvent(id, 1, "succeeded")); QTRY_COMPARE(c.state(), PlanningClient::State::PlanReady);
    s.autoSync = false; c.query("get_scene"); QTRY_COMPARE(s.count("get_scene"), 2);
    auto scene = fixture().value("scene").toObject(); scene.insert("scene_revision", "new-revision"); s.reply(s.request("get_scene"), scene);
    QTRY_VERIFY(c.planId().isEmpty()); QVERIFY(c.preview().isEmpty());
    auto p = payload(); p.insert("scene_revision", "new-revision"); id = c.plan("plan_pick_place", p);
    auto e = planningEvent(id, 1, "succeeded"); auto r = e.value("payload").toObject(); r.insert("scene_revision", "new-revision");
    auto v = r.value("validation").toObject(); v.insert("simulated", false); r.insert("validation", v); e.insert("payload", r); s.send(e);
    QTRY_COMPARE(c.state(), PlanningClient::State::Failed); QVERIFY(c.planId().isEmpty());
    c.query("get_capabilities"); QTRY_COMPARE(s.count("get_capabilities"), 2);
    auto caps = fixture().value("capabilities").toObject(); caps.insert("robot_model_id", "changed-model"); s.reply(s.request("get_capabilities"), caps);
    QTRY_VERIFY(c.scene().isEmpty()); QVERIFY(!c.canStart());
}
void TestPlanning::ndjsonLimitsAndFragments() {
    NdjsonParser p(32); QVERIFY(p.append("{\"a\":").isEmpty());
    auto frames = p.append("1}\r\n{}\n[1]\ninvalid\n"); QCOMPARE(frames.size(), 4);
    QVERIFY(frames[0].valid && frames[1].valid && !frames[2].valid && !frames[3].valid);
    frames = p.append(QByteArray(33, 'x')); QCOMPARE(frames.size(), 1); QVERIFY(!frames[0].valid); QVERIFY(p.bufferedData().isEmpty());
    QVERIFY(p.append("garbage").isEmpty()); frames = p.append("\n{}\n"); QCOMPARE(frames.size(), 1); QVERIFY(frames[0].valid);
    frames = p.append(QByteArray(33, 'x') + "\n{}\n"); QCOMPARE(frames.size(), 2); QVERIFY(!frames[0].valid && frames[1].valid);
    p.clear(); const auto bytes = QStringLiteral("{\"s\":\"quỹ\"}\n").toUtf8();
    for (int i = 0; i < bytes.size() - 1; ++i) QVERIFY(p.append(bytes.mid(i, 1)).isEmpty());
    frames = p.append(bytes.right(1)); QCOMPARE(frames.size(), 1); QCOMPARE(frames[0].object.value("s").toString(), QStringLiteral("quỹ"));
    NdjsonParser exact(1024 * 1024); auto huge = QByteArray("{\"s\":\"") + QByteArray(1024 * 1024 - 8, 'a') + "\"}\n";
    QVERIFY(exact.append(huge).first().valid);
}
void TestPlanning::capabilityGatingAndOldSceneResponse() {
    Server s; PlanningClient c; s.autoSync = false; s.open(c);
    QTRY_COMPARE(s.count("get_capabilities"), 1);
    auto caps = fixture().value("capabilities").toObject(); caps.insert("execution_enabled", true);
    s.reply(s.request("get_capabilities"), caps); QTest::qWait(20); QVERIFY(!c.canStart());
    c.reconcile(); QTRY_COMPARE(s.count("get_capabilities"), 2);
    caps = fixture().value("capabilities").toObject();
    QJsonArray commands;
    for (const auto &command : caps.value("supported_commands").toArray()) if (command != "plan_pick_place") commands.append(command);
    caps.insert("supported_commands", commands);
    s.reply(s.request("get_capabilities"), caps); QTRY_COMPARE(s.count("get_scene"), 1);
    s.reply(s.request("get_scene"), fixture().value("scene").toObject()); QTRY_VERIFY(c.canStart());
    QVERIFY(!c.supports("plan_pick_place")); QVERIFY(c.plan("plan_pick_place", payload()).isEmpty());
    c.query("get_scene"); QTRY_COMPARE(s.count("get_scene"), 2); const auto oldRequest = s.request("get_scene");
    c.query("get_scene"); QTRY_COMPARE(s.count("get_scene"), 3); const auto newRequest = s.request("get_scene");
    auto scene = fixture().value("scene").toObject(); scene.insert("scene_revision", "newer"); s.reply(newRequest, scene);
    QTRY_COMPARE(c.scene().value("scene_revision").toString(), QString("newer"));
    s.reply(oldRequest, fixture().value("scene").toObject()); QTest::qWait(20);
    QCOMPARE(c.scene().value("scene_revision").toString(), QString("newer"));
    c.query("get_scene"); QTRY_COMPARE(s.count("get_scene"), 4);
    auto directions = scene.value("cartesian_directions").toObject(); directions.insert("lift", QJsonObject{{"frame_id", "mock_world"}, {"vector", QJsonArray{0,0,0}}});
    scene.insert("cartesian_directions", directions); s.reply(s.request("get_scene"), scene);
    QTRY_VERIFY(!c.synchronized()); QVERIFY(!c.canStart());
}
void TestPlanning::invalidInput() {
    QString error; const auto good = payload(); QVERIFY(PlanningInput::request("plan_pick_place", good, 60, &error));
    for (const auto &key : {"velocity_scale", "acceleration_scale", "planning_timeout_s"})
        for (const QJsonValue &bad : {QJsonValue(0), QJsonValue(-1), QJsonValue(61), QJsonValue("NaN"), QJsonValue(QJsonValue::Null)}) {
            auto p = good; p.insert(key, bad); QVERIFY(!PlanningInput::request("plan_pick_place", p, 60, &error));
        }
    for (const auto &key : {"approach_distance_m", "lift_distance_m", "retreat_distance_m"}) {
        auto p = good; p.insert(key, -0.1); QVERIFY(!PlanningInput::request("plan_pick_place", p, 60));
        p.insert(key, 0); QVERIFY(PlanningInput::request("plan_pick_place", p, 60));
    }
    auto pose = good.value("pick_tcp_pose").toObject();
    for (const auto &q : {QJsonArray{0,0,0,0}, QJsonArray{0,0,0,2}, QJsonArray{0,0,1}, QJsonArray{0,0,"Inf",1}}) {
        pose.insert("orientation_xyzw", q); QVERIFY(!PlanningInput::pose(pose));
    }
    pose = good.value("pick_tcp_pose").toObject(); pose.insert("frame_id", " "); QVERIFY(!PlanningInput::pose(pose));
}
void TestPlanning::panelUsesSceneAndRejectsNan() {
    Server s; PlanningPanel panel; auto *c = panel.findChild<PlanningClient *>(); QVERIFY(c);
    s.open(*c); QTRY_VERIFY(c->canStart());
    const auto p = fixture().value("scene").toObject().value("pick_tcp_pose").toObject().value("position").toArray();
    auto *x = panel.findChild<QLineEdit *>("pickPose0"); QVERIFY(x); QCOMPARE(x->text().toDouble(), p[0].toDouble());
    QPushButton *plan = nullptr;
    for (auto *b : panel.findChildren<QPushButton *>()) { QVERIFY(!b->text().contains("Execute")); if (b->text() == "Plan pose") plan = b; }
    QVERIFY(plan && plan->isEnabled());
    for (const auto &draft : {"NaN", "Inf", "-Inf", ""}) {
        x->setText(draft); plan->click(); QTest::qWait(20); QCOMPARE(s.count("plan_to_pose"), 0);
    }
    x->setText("0.5"); panel.resize(1200, 840); panel.show(); QTest::qWait(50);
    QVERIFY(panel.grab().save("planning-panel-1200x840.png"));
    x->setText("0.5"); plan->click(); QTRY_COMPARE(s.count("plan_to_pose"), 1);
}
void TestPlanning::mockIntegration_data() {
    QTest::addColumn<QString>("mode");
    for (const auto &mode : {"normal", "terminal-first", "failure", "timeout", "disconnect", "stale-revision", "cancel"})
        QTest::newRow(mode) << QString(mode);
}
void TestPlanning::mockIntegration() {
    QFETCH(QString, mode);
    QProcess process; QStringList args{QStringLiteral(PLANNING_MOCK_PATH), "--port", "0", "--delay", "0.15", "--coalesce", "--fragment"};
    if (mode != "cancel") args << "--mode" << mode;
    process.start("python", args); QVERIFY(process.waitForStarted()); QVERIFY(process.waitForReadyRead());
    const auto output = process.readAllStandardOutput();
    const QRegularExpression re("\\('127\\.0\\.0\\.1', (\\d+)\\)"); const auto match = re.match(QString::fromUtf8(output)); QVERIFY2(match.hasMatch(), output.constData());
    PlanningClient c; c.connectToServer("127.0.0.1", quint16(match.captured(1).toUInt())); QTRY_VERIFY_WITH_TIMEOUT(c.canStart(), 5000);
    auto p = payload(); if (mode == "timeout") p.insert("planning_timeout_s", .03);
    const auto id = c.plan("plan_pick_place", p); QVERIFY(!id.isEmpty());
    if (mode == "cancel") QVERIFY(!c.cancel().isEmpty());
    if (mode == "disconnect") {
        QTRY_COMPARE(c.state(), PlanningClient::State::Disconnected);
        c.connectToServer("127.0.0.1", quint16(match.captured(1).toUInt()));
    }
    const auto expected = mode == "failure" || mode == "timeout" ? PlanningClient::State::Failed
        : mode == "cancel" ? PlanningClient::State::Cancelled : PlanningClient::State::PlanReady;
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), expected, 5000);
    if (expected == PlanningClient::State::PlanReady) {
        QTRY_VERIFY_WITH_TIMEOUT(c.synchronized(), 5000);
        QCOMPARE(c.result().value("validation").toObject().value("simulated").toBool(), true);
        QVERIFY(!c.preview().isEmpty());
        if (mode == "stale-revision") QTRY_VERIFY(c.planId().isEmpty());
    }
    process.kill(); QVERIFY(process.waitForFinished());
}
QTEST_MAIN(TestPlanning)
#include "TestPlanning.moc"
