#include "ui/PlanningPanel.hpp"
#include "planning/PlanningClient.hpp"
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QGroupBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaEnum>
#include <cmath>

namespace {
QString json(const QJsonValue &v) {
    return QString::fromUtf8(v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)
        : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
}
}
PlanningPanel::PlanningPanel(QWidget *parent) : QWidget(parent), client_(new PlanningClient(this)) {
    setObjectName("planningPanel");
    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); outer->addWidget(scroll);
    auto *content = new QWidget; scroll->setWidget(content); auto *layout = new QVBoxLayout(content);
    auto *connection = new QHBoxLayout;
    connection->addWidget(new QLabel("Planning host:")); host_ = new QLineEdit("127.0.0.1");
    host_->setObjectName("planningHost"); connection->addWidget(host_);
    connection->addWidget(new QLabel("Port:")); port_ = new QLineEdit("8082"); port_->setMaximumWidth(90);
    connection->addWidget(port_); connect_ = new QPushButton("Kết nối planning"); connection->addWidget(connect_);
    reconcile_ = new QPushButton("Đối chiếu status"); connection->addWidget(reconcile_); layout->addLayout(connection);
    capability_ = new QLabel("Chưa có capability/model/frame/TCP từ backend."); capability_->setWordWrap(true);
    capability_->setTextFormat(Qt::PlainText); layout->addWidget(capability_);
    auto *selection = new QHBoxLayout; scenario_ = new QComboBox; object_ = new QComboBox;
    selection->addWidget(new QLabel("Scenario:")); selection->addWidget(scenario_);
    selection->addWidget(new QLabel("Vật gắp:")); selection->addWidget(object_);
    load_ = new QPushButton("Nạp scene"); selection->addWidget(load_); layout->addLayout(selection);
    objects_ = new QTableWidget(0, 5); objects_->setHorizontalHeaderLabels({"ID / vai trò", "Geometry", "Kích thước (m)", "Frame", "Pose: m; quaternion xyzw"});
    objects_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    objects_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    objects_->horizontalHeader()->setStretchLastSection(true); objects_->setMinimumHeight(190); layout->addWidget(objects_);
    auto makePose = [layout](const QString &title, const QString &prefix, QVector<QLineEdit *> &fields, QLineEdit *&frame) {
        auto *box = new QGroupBox(title); auto *grid = new QGridLayout(box);
        grid->addWidget(new QLabel("frame_id:"), 0, 0); frame = new QLineEdit; grid->addWidget(frame, 0, 1, 1, 6);
        const QStringList labels{"x (m)", "y (m)", "z (m)", "qx", "qy", "qz", "qw"};
        for (int i = 0; i < 7; ++i) {
            grid->addWidget(new QLabel(labels[i]), 1, i); auto *edit = new QLineEdit;
            edit->setObjectName(prefix + QString::number(i)); edit->setMinimumWidth(65);
            edit->setPlaceholderText("Từ scene"); grid->addWidget(edit, 2, i); fields.append(edit);
        }
        layout->addWidget(box);
    };
    makePose("Pick TCP pose / goal TCP cho Plan pose — quaternion [x,y,z,w], norm=1", "pickPose", pick_, pickFrame_);
    makePose("Place object pose — pose của vật sau thả", "placePose", place_, placeFrame_);
    directions_ = new QLabel; directions_->setWordWrap(true); directions_->setTextFormat(Qt::PlainText); layout->addWidget(directions_);
    auto *params = new QGridLayout;
    const QStringList keys{"approach_distance_m", "lift_distance_m", "retreat_distance_m", "planning_timeout_s", "velocity_scale", "acceleration_scale"};
    const QStringList labels{"Approach (m)", "Lift (m)", "Retreat (m)", "Timeout (s)", "Velocity scale (0,1]", "Acceleration scale (0,1]"};
    for (int i = 0; i < keys.size(); ++i) {
        params->addWidget(new QLabel(labels[i]), 0, i); auto *edit = new QLineEdit;
        edit->setObjectName(keys[i]); edit->setPlaceholderText("Từ scene");
        parameters_.insert(keys[i], edit); params->addWidget(edit, 1, i);
    }
    layout->addLayout(params);
    auto *actions = new QHBoxLayout; pose_ = new QPushButton("Plan pose"); task_ = new QPushButton("Plan gắp–thả");
    preview_ = new QPushButton("Xem trước RViz"); cancel_ = new QPushButton("Hủy planning");
    for (auto *b : {pose_, task_, preview_, cancel_}) actions->addWidget(b);
    // Keep actions and progress visible while the scene/form/result area scrolls.
    outer->addLayout(actions);
    state_ = new QLabel; stage_ = new QLabel; stage_->setTextFormat(Qt::PlainText);
    outer->addWidget(state_); outer->addWidget(stage_); progress_ = new QProgressBar; progress_->setRange(0, 100); outer->addWidget(progress_);
    results_ = new QPlainTextEdit; results_->setReadOnly(true); results_->setMinimumHeight(140); layout->addWidget(results_);
    log_ = new QPlainTextEdit; log_->setReadOnly(true); log_->setMaximumBlockCount(500); log_->setMinimumHeight(110); layout->addWidget(log_);
    connect(client_, &PlanningClient::message, log_, &QPlainTextEdit::appendPlainText);
    connect(client_, &PlanningClient::changed, this, &PlanningPanel::updateView);
    connect(client_, &PlanningClient::eventReceived, this, [this](const QJsonObject &e) {
        stage_->setText(QString("Stage: %1; status: %2; event_seq: %3").arg(e.value("stage").toString(), e.value("status").toString()).arg(e.value("event_seq").toDouble()));
        const double p = e.value("payload").toObject().value("progress").toDouble(0);
        progress_->setValue(e.value("status") == "succeeded" ? 100 : int(qBound(0.0, p, 1.0) * 100));
    });
    connect(client_, &PlanningClient::responseReceived, this, [this](const QString &, const QString &command, const QJsonObject &e) {
        if (!e.value("ok").toBool()) return;
        if (command == "get_scene" || command == "load_test_scene") fillScene();
        if (command == "preview_plan") log_->appendPlainText("Preview metadata: " + json(e.value("payload")));
    });
    connect(connect_, &QPushButton::clicked, this, [this] {
        bool ok = false; const int port = port_->text().toInt(&ok);
        if (!ok || port < 1 || port > 65535 || host_->text().trimmed().isEmpty()) {
            log_->appendPlainText("INVALID_REQUEST: host/port không hợp lệ."); return;
        }
        client_->connectToServer(host_->text().trimmed(), quint16(port));
    });
    connect(reconcile_, &QPushButton::clicked, client_, &PlanningClient::reconcile);
    connect(load_, &QPushButton::clicked, this, [this] { client_->loadScene(scenario_->currentText()); });
    connect(pose_, &QPushButton::clicked, this, [this] { submit("plan_to_pose"); });
    connect(task_, &QPushButton::clicked, this, [this] { submit("plan_pick_place"); });
    connect(cancel_, &QPushButton::clicked, client_, &PlanningClient::cancel);
    connect(preview_, &QPushButton::clicked, client_, &PlanningClient::preview);
    updateView();
}
void PlanningPanel::updateView() {
    const auto caps = client_->capabilities();
    if (!caps.isEmpty()) {
        const bool mock = caps.value("backend_mode") == "mock";
        capability_->setText(QString("%1 | Model/version: %2 / %3 | robot_model_id: %4\nFrame: %5 | TCP/link: %6\nCommands: %7 | TTL plan/status: %8 / %9 s | execution_enabled=false%10")
            .arg(mock ? "MOCK — chỉ mô phỏng protocol; chưa tính IK/collision/trajectory thật" : "FAKE HARDWARE — ROS 2 planning, cần xác minh model/scene",
                caps.value("model").toString(), caps.value("model_version").toString(), caps.value("robot_model_id").toString(),
                caps.value("planning_frame").toString(), json(caps.value("tcp_mappings")), json(caps.value("supported_commands")))
            .arg(caps.value("plan_ttl_s").toDouble()).arg(caps.value("task_status_ttl_s").toDouble())
            .arg(mock ? "\nMOCK preview không mở RViz." : ""));
        capability_->setStyleSheet(mock ? "color:#b55b00;font-weight:bold" : "");
        QStringList scenarios; for (const auto &s : caps.value("scenarios").toArray()) scenarios.append(s.toString());
        QStringList existing; for (int i = 0; i < scenario_->count(); ++i) existing.append(scenario_->itemText(i));
        if (existing != scenarios) { const auto selected = scenario_->currentText(); scenario_->clear(); scenario_->addItems(scenarios); scenario_->setCurrentText(selected); }
    }
    const bool ready = client_->canStart();
    load_->setEnabled(ready && client_->supports("load_test_scene"));
    pose_->setEnabled(ready && client_->supports("plan_to_pose"));
    task_->setEnabled(ready && client_->supports("plan_pick_place"));
    preview_->setEnabled(ready && client_->state() == PlanningClient::State::PlanReady && client_->supports("preview_plan"));
    cancel_->setEnabled(client_->busy() && client_->state() != PlanningClient::State::Disconnected
        && client_->state() != PlanningClient::State::Cancelling && client_->supports("cancel_planning"));
    scenario_->setEnabled(ready); object_->setEnabled(ready);
    for (auto *e : pick_) e->setEnabled(ready); for (auto *e : place_) e->setEnabled(ready);
    pickFrame_->setEnabled(ready); placeFrame_->setEnabled(ready);
    for (auto *e : parameters_) e->setEnabled(ready);
    const auto meta = QMetaEnum::fromType<PlanningClient::State>();
    state_->setText(QString("Planning state: %1 | %2 | task_id: %3 | scene revision: %4")
        .arg(QString::fromLatin1(meta.valueToKey(int(client_->state()))), client_->synchronized() ? "Đã đối chiếu" : "Chưa đồng bộ",
            client_->taskId(), client_->scene().value("scene_revision").toString()));
    results_->setPlainText(client_->result().isEmpty() ? "Chưa có plan hợp lệ. ACK chỉ xác nhận nhận yêu cầu."
        : QString::fromUtf8(QJsonDocument(client_->result()).toJson(QJsonDocument::Indented)));
}
void PlanningPanel::fillScene() {
    const auto scene = client_->scene();
    const auto identity = scene.value("robot_model_id").toString() + "/" + scene.value("scene_revision").toString();
    if (scene.isEmpty() || identity == displayedRevision_) return;
    displayedRevision_ = identity;
    const auto objects = scene.value("objects").toArray(); objects_->setRowCount(objects.size()); object_->clear();
    for (int i = 0; i < objects.size(); ++i) {
        const auto o = objects[i].toObject(); const auto p = o.value("pose").toObject();
        const QStringList values{o.value("id").toString() + " / " + o.value("role").toString(),
            o.value("geometry").toString(), json(o.value("dimensions_m")), p.value("frame_id").toString(),
            json(p.value("position")) + " / " + json(p.value("orientation_xyzw"))};
        for (int c = 0; c < values.size(); ++c) objects_->setItem(i, c, new QTableWidgetItem(values[c]));
        if (o.value("role") == "target") object_->addItem(o.value("id").toString());
    }
    auto fillPose = [](const QJsonObject &p, const QVector<QLineEdit *> &fields, QLineEdit *frame) {
        frame->setText(p.value("frame_id").toString()); const auto xyz = p.value("position").toArray();
        const auto q = p.value("orientation_xyzw").toArray();
        for (int i = 0; i < 7; ++i) fields[i]->setText(QString::number(i < 3 ? xyz.at(i).toDouble() : q.at(i - 3).toDouble(), 'g', 15));
    };
    fillPose(scene.value("pick_tcp_pose").toObject(), pick_, pickFrame_);
    fillPose(scene.value("place_object_pose").toObject(), place_, placeFrame_);
    const auto settings = scene.value("defaults").toObject();
    for (auto it = parameters_.begin(); it != parameters_.end(); ++it)
        it.value()->setText(settings.value(it.key()).isDouble() ? QString::number(settings.value(it.key()).toDouble(), 'g', 15) : "");
    directions_->setText("Hướng Cartesian từ scene (frame_id, vector): " + json(scene.value("cartesian_directions"))
        + "\nMarkers (pose m/xyzw): " + json(scene.value("markers"))
        + "\nPlace TCP do backend suy ra: T_world_object_place * inverse(T_tcp_object); gồm TCP–link offset. Tọa độ chưa chứng minh reachable/an toàn.");
    stage_->clear(); progress_->setValue(0); updateView();
}
QJsonObject PlanningPanel::readPose(const QVector<QLineEdit *> &edits, const QString &frame, bool &valid) {
    QJsonArray pos, q;
    for (int i = 0; i < edits.size(); ++i) {
        bool ok; const double v = edits[i]->text().toDouble(&ok); valid &= ok && std::isfinite(v);
        if (i < 3) pos.append(v); else q.append(v);
    }
    return {{"frame_id", frame.trimmed()}, {"position", pos}, {"orientation_xyzw", q}};
}
void PlanningPanel::submit(const QString &command) {
    bool valid = true; const auto scene = client_->scene();
    QJsonObject p{{"scene_revision", scene.value("scene_revision")}, {"group", scene.value("group")}, {"tcp_frame", scene.value("tcp_frame")}};
    const auto pick = readPose(pick_, pickFrame_->text(), valid);
    if (command == "plan_to_pose") p.insert("goal_tcp_pose", pick);
    else { p.insert("pick_tcp_pose", pick); p.insert("place_object_pose", readPose(place_, placeFrame_->text(), valid)); p.insert("object_id", object_->currentText()); }
    for (auto it = parameters_.begin(); it != parameters_.end(); ++it) {
        if (command == "plan_to_pose" && it.key().endsWith("distance_m")) continue;
        bool ok; const double v = it.value()->text().toDouble(&ok); valid &= ok && std::isfinite(v); p.insert(it.key(), v);
    }
    if (!valid) { log_->appendPlainText("INVALID_REQUEST: từ chối ô trống, không phải số, NaN hoặc Inf."); return; }
    if (!client_->plan(command, p).isEmpty()) { stage_->setText("Chờ ACK / progress / terminal event"); progress_->setValue(0); }
}
