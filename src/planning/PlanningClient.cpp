#include "planning/PlanningClient.hpp"
#include "planning/PlanningInput.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <cmath>

PlanningClient::PlanningClient(QObject *parent) : QObject(parent),
    session_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    connect(&socket_, &QTcpSocket::connected, this, &PlanningClient::reconcile);
    connect(&socket_, &QTcpSocket::disconnected, this, [this] {
        pending_.clear(); parser_.clear(); refresh_.stop();
        synced_ = reconciling_ = false; syncId_.clear(); sceneMutationId_.clear();
        uncertain_ = busy();
        setState(State::Disconnected);
        emit message("Planning disconnected; kết quả task chưa được xác nhận. Kết nối lại để đối chiếu.");
    });
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this] {
        emit message(socket_.errorString()); emit changed();
    });
    connect(&socket_, &QTcpSocket::readyRead, this, [this] {
        const auto frames = parser_.append(socket_.readAll());
        for (const auto &frame : frames) {
            if (!frame.valid) { emit message(frame.error); socket_.abort(); break; }
            receive(frame.object);
        }
    });
    refresh_.setInterval(2000);
    connect(&refresh_, &QTimer::timeout, this, [this] {
        if (!synced_ || reconciling_ || !sceneMutationId_.isEmpty()) return;
        bool capsPending = false, scenePending = false, statusPending = false;
        for (const auto &p : pending_) {
            capsPending |= p.command == "get_capabilities";
            scenePending |= p.command == "get_scene";
            statusPending |= p.command == "get_task_status";
        }
        if (!capsPending) query("get_capabilities");
        if (supports("get_scene") && !scenePending) query("get_scene");
        if (busy() && supports("get_task_status") && !statusPending)
            query("get_task_status", {{"target_request_id", taskId_}});
    });
}
PlanningClient::~PlanningClient() { socket_.disconnect(this); socket_.abort(); }
void PlanningClient::connectToServer(const QString &host, quint16 port) {
    socket_.abort();
    synced_ = false; pending_.clear(); parser_.clear();
    setState(State::Disconnected);
    socket_.connectToHost(host, port);
}
void PlanningClient::disconnectFromServer() { socket_.abort(); }
void PlanningClient::setState(State value) { state_ = value; emit changed(); }
bool PlanningClient::supports(const QString &command) const {
    return capabilities_.value("supported_commands").toArray().contains(command);
}
bool PlanningClient::canStart() const {
    return synced_ && !reconciling_ && !busy() && sceneMutationId_.isEmpty()
        && !scene_.isEmpty() && socket_.state() == QAbstractSocket::ConnectedState;
}
QString PlanningClient::send(const QString &command, const QJsonObject &payload, int timeoutMs) {
    if (socket_.state() != QAbstractSocket::ConnectedState || timeoutMs <= 0) return {};
    const QString id = session_ + "-" + QString::number(++counter_);
    const QJsonObject envelope{{"protocol_version", 1}, {"type", "request"},
        {"request_id", id}, {"command", command}, {"payload", payload}};
    const auto bytes = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    const int limit = qMin(1024 * 1024, capabilities_.value("max_frame_bytes").toInt(1024 * 1024));
    if (bytes.size() > limit) { emit message("Planning request exceeds frame limit."); return {}; }
    pending_.insert(id, {command, payload, counter_});
    if (socket_.write(bytes + '\n') < 0) { pending_.remove(id); return {}; }
    QTimer::singleShot(timeoutMs, this, [this, id] {
        if (!pending_.contains(id)) return;
        const auto p = pending_.take(id);
        if (id == syncId_) { syncId_.clear(); reconciling_ = false; synced_ = false; }
        if (id == sceneMutationId_) { sceneMutationId_.clear(); synced_ = false; }
        if (id == taskId_ && !taskTerminal_) {
            uncertain_ = true; synced_ = false;
            setState(cancelRequested_ ? State::Cancelling : State::Failed);
        }
        emit requestTimedOut(id, p.command);
        emit message("TIMEOUT: " + p.command + "; chưa xác nhận kết thúc/hủy. Dùng Đối chiếu status.");
        emit changed();
    });
    return id;
}
QString PlanningClient::query(const QString &command, const QJsonObject &payload, int timeoutMs) {
    if (command != "get_capabilities" && command != "get_scene" && command != "get_task_status")
        return {};
    if (command != "get_capabilities" && !supports(command)) return {};
    if (command == "get_task_status" && payload.value("target_request_id").toString() != taskId_)
        return {};
    return send(command, payload, timeoutMs);
}
void PlanningClient::reconcile() {
    if (socket_.state() != QAbstractSocket::ConnectedState || reconciling_) return;
    synced_ = false; reconciling_ = true;
    syncId_ = send("get_capabilities", {}, 3000);
    emit changed();
}
bool PlanningClient::applyCapabilities(const QJsonObject &p) {
    const auto mode = p.value("backend_mode").toString();
    if ((mode != "mock" && mode != "fake_hardware") || !p.value("execution_enabled").isBool()
        || p.value("execution_enabled").toBool() || p.value("robot_model_id").toString().isEmpty()
        || p.value("model").toString().isEmpty() || p.value("model_version").toString().isEmpty()
        || p.value("planning_frame").toString().isEmpty() || p.value("groups").toArray().isEmpty()
        || p.value("tcp_mappings").toArray().isEmpty()
        || !p.value("supported_commands").toArray().contains("get_scene")
        || !p.value("supported_commands").toArray().contains("get_task_status")
        || p.value("max_frame_bytes").toInt() <= 0
        || p.value("max_planning_timeout_s").toDouble() <= 0
        || p.value("max_planning_timeout_s").toDouble() > 3600
        || p.value("plan_ttl_s").toDouble() <= 0 || p.value("task_status_ttl_s").toDouble() <= 0) {
        synced_ = false;
        emit message("INVALID_REQUEST: capability thiếu/không hỗ trợ backend planning-only an toàn.");
        return false;
    }
    if (!capabilities_.isEmpty() && (p.value("robot_model_id") != capabilities_.value("robot_model_id")
        || p.value("planning_frame") != capabilities_.value("planning_frame")
        || p.value("tcp_mappings") != capabilities_.value("tcp_mappings"))) {
        invalidatePlan(); scene_ = {};
    }
    capabilities_ = p;
    emit changed(); return true;
}
bool PlanningClient::applyScene(const QJsonObject &p) {
    QString error;
    const auto units = p.value("units").toObject();
    if (p.value("schema_version") != QJsonValue(1) || units.value("length") != "m"
        || units.value("angle") != "rad" || units.value("quaternion") != "xyzw"
        || p.value("scene_revision").toString().isEmpty()
        || p.value("robot_model_id") != capabilities_.value("robot_model_id")
        || p.value("frame_id") != capabilities_.value("planning_frame")
        || (p.value("group") != "right_arm" && p.value("group") != "left_arm")
        || !capabilities_.value("groups").toArray().contains(p.value("group"))
        || !PlanningInput::pose(p.value("pick_tcp_pose").toObject(), &error)
        || !PlanningInput::pose(p.value("place_object_pose").toObject(), &error)
        || p.value("objects").toArray().isEmpty() || p.value("cartesian_directions").toObject().isEmpty()) {
        synced_ = false; emit message("INVALID_REQUEST: scene snapshot không hợp lệ. " + error);
        return false;
    }
    bool mapping = false;
    for (const auto &v : capabilities_.value("tcp_mappings").toArray()) {
        const auto m = v.toObject();
        mapping |= m.value("group") == p.value("group") && m.value("tcp_frame") == p.value("tcp_frame");
    }
    if (!mapping) { synced_ = false; emit message("Scene TCP/group không có capability."); return false; }
    for (const auto &key : {"approach", "lift", "lower", "retreat"}) {
        const auto direction = p.value("cartesian_directions").toObject().value(key).toObject();
        const auto vector = direction.value("vector").toArray();
        double norm = 0;
        if (direction.value("frame_id").toString().isEmpty() || vector.size() != 3) {
            synced_ = false; emit message("Scene thiếu hướng Cartesian/frame."); return false;
        }
        for (const auto &v : vector) {
            if (!v.isDouble() || !std::isfinite(v.toDouble())) { synced_ = false; return false; }
            norm += v.toDouble() * v.toDouble();
        }
        if (!std::isfinite(norm) || std::abs(norm - 1) > .002001) { synced_ = false; emit message("Scene direction phải có norm=1."); return false; }
    }
    for (const auto &v : p.value("objects").toArray()) {
        const auto object = v.toObject();
        const auto dimensions = object.value("dimensions_m").toArray();
        const auto geometry = object.value("geometry").toString();
        if (object.value("id").toString().isEmpty() || !PlanningInput::pose(object.value("pose").toObject())
            || (geometry != "box" && geometry != "cylinder")
            || dimensions.size() != (geometry == "box" ? 3 : 2)) { synced_ = false; return false; }
        for (const auto &d : dimensions)
            if (!d.isDouble() || !std::isfinite(d.toDouble()) || d.toDouble() <= 0) { synced_ = false; return false; }
    }
    if (!scene_.isEmpty() && p.value("scene_revision") != scene_.value("scene_revision")) invalidatePlan();
    if (!result_.isEmpty() && (p.value("scene_revision") != result_.value("scene_revision")
        || p.value("robot_model_id") != result_.value("robot_model_id"))) invalidatePlan();
    scene_ = p; emit changed(); return true;
}
void PlanningClient::invalidatePlan() {
    result_ = {};
    if (state_ == State::PlanReady) setState(State::Idle);
    emit message("STALE_PLAN: scene/model/frame/TCP thay đổi; plan mất hiệu lực.");
}
QString PlanningClient::loadScene(const QString &scenario) {
    if (!canStart() || !supports("load_test_scene")
        || !capabilities_.value("scenarios").toArray().contains(scenario)) return {};
    sceneMutationId_ = send("load_test_scene", {{"scenario_id", scenario}}, 3000);
    if (!sceneMutationId_.isEmpty()) { invalidatePlan(); emit changed(); }
    return sceneMutationId_;
}
QString PlanningClient::plan(const QString &command, const QJsonObject &payload) {
    QString error;
    if (!canStart() || !supports(command)) { emit message("Planning chưa đồng bộ, đang bận hoặc thiếu capability."); return {}; }
    if (!PlanningInput::request(command, payload, capabilities_.value("max_planning_timeout_s").toDouble(), &error)) {
        emit message("INVALID_REQUEST: " + error); return {};
    }
    if (payload.value("scene_revision") != scene_.value("scene_revision")
        || payload.value("group") != scene_.value("group") || payload.value("tcp_frame") != scene_.value("tcp_frame")) {
        emit message("STALE_PLAN: revision/group/TCP khác scene."); return {};
    }
    if (command == "plan_pick_place") {
        bool found = false;
        for (const auto &o : scene_.value("objects").toArray())
            found |= o.toObject().value("id") == payload.value("object_id") && o.toObject().value("role") == "target";
        if (!found) { emit message("INVALID_REQUEST: object_id không phải vật gắp trong scene."); return {}; }
    }
    taskId_ = send(command, payload, 3000);
    if (taskId_.isEmpty()) return {};
    taskCommand_ = command; lastStatus_ = "planning"; seq_ = 0; taskTerminal_ = false; cancelRequested_ = uncertain_ = false;
    result_ = {}; taskRevision_ = scene_.value("scene_revision").toString();
    taskModel_ = capabilities_.value("robot_model_id").toString();
    taskGroup_ = payload.value("group").toString(); taskTcp_ = payload.value("tcp_frame").toString();
    taskFrame_ = capabilities_.value("planning_frame").toString();
    const auto generation = ++taskGeneration_;
    const int deadline = int(std::ceil(payload.value("planning_timeout_s").toDouble() * 1000)) + 1500;
    QTimer::singleShot(deadline, this, [this, generation] {
        if (generation != taskGeneration_ || taskTerminal_) return;
        pending_.remove(taskId_); uncertain_ = true; synced_ = false;
        setState(cancelRequested_ ? State::Cancelling : State::Failed);
        emit requestTimedOut(taskId_, taskCommand_);
        emit message("TIMEOUT: chưa nhận terminal event; không coi là đã hủy. Đối chiếu status.");
    });
    setState(State::Planning); return taskId_;
}
QString PlanningClient::cancel() {
    if (!busy() || !supports("cancel_planning") || cancelRequested_) return {};
    const auto id = send("cancel_planning", {{"target_request_id", taskId_}}, 3000);
    if (!id.isEmpty()) { cancelRequested_ = true; result_ = {}; setState(State::Cancelling); }
    return id;
}
QString PlanningClient::preview() {
    if (!canStart() || state_ != State::PlanReady || !supports("preview_plan") || result_.isEmpty()) return {};
    return send("preview_plan", {{"plan_id", planId()}}, 3000);
}
void PlanningClient::reportError(const QJsonObject &e) {
    const auto err = e.value("error").toObject();
    emit message(QString("%1: %2; stage=%3; details=%4").arg(err.value("code").toString(),
        err.value("message").toString(), err.value("stage").toString(),
        QString::fromUtf8(QJsonDocument(err.value("details").toObject()).toJson(QJsonDocument::Compact))));
}
void PlanningClient::finishSync() {
    synced_ = true; reconciling_ = false; syncId_.clear();
    if (state_ == State::Disconnected) {
        if (busy()) setState(cancelRequested_ ? State::Cancelling : State::Planning);
        else if (!result_.isEmpty()) setState(State::PlanReady);
        else if (lastStatus_ == "cancelled") setState(State::Cancelled);
        else if (lastStatus_ == "failed" || cancelRequested_) setState(State::Failed);
        else setState(State::Idle);
    }
    refresh_.start(); emit changed();
}
void PlanningClient::receive(const QJsonObject &e) {
    if (e.value("protocol_version") != QJsonValue(1) || !e.value("request_id").isString()) return;
    const auto id = e.value("request_id").toString();
    const auto command = e.value("command").toString();
    if (e.value("type") == "event") {
        if (!uncertain_ && !reconciling_) applyEvent(e);
        return;
    }
    if (e.value("type") != "response" || !e.value("ok").isBool()
        || !pending_.contains(id) || pending_.value(id).command != command) return;
    const auto pending = pending_.take(id);
    const bool sync = id == syncId_;
    const bool mutation = id == sceneMutationId_;
    if (mutation) sceneMutationId_.clear();
    if (!e.value("ok").toBool()) {
        reportError(e);
        if (id == taskId_ && !taskTerminal_) {
            taskTerminal_ = true; lastStatus_ = "failed"; uncertain_ = false; setState(State::Failed);
        }
        if (command == "preview_plan" && e.value("error").toObject().value("code") == "STALE_PLAN") invalidatePlan();
        if (sync) { reconciling_ = false; synced_ = false; syncId_.clear(); }
        emit responseReceived(id, command, e); emit changed(); return;
    }
    const auto p = e.value("payload").toObject();
    bool valid = true;
    if (command == "get_capabilities" && pending.serial >= latestCapabilitySerial_) {
        latestCapabilitySerial_ = pending.serial; valid = applyCapabilities(p);
    }
    if ((command == "get_scene" || command == "load_test_scene") && pending.serial >= latestSceneSerial_) {
        latestSceneSerial_ = pending.serial; valid = applyScene(p);
    }
    if (command == "get_task_status") {
        if (pending.payload.value("target_request_id") != taskId_ || p.value("request_id") != taskId_
            || p.value("command") != taskCommand_) valid = false;
        else {
            auto snapshot = p; snapshot.insert("type", "event"); snapshot.insert("protocol_version", 1);
            valid = applyEvent(snapshot, true);
            if (valid) uncertain_ = false;
        }
        if (!valid) emit message("Task status thiếu/sai ID, command hoặc sequence; vẫn khóa planning mới.");
    }
    emit responseReceived(id, command, e);
    if (sync) {
        if (!valid) { reconciling_ = false; synced_ = false; syncId_.clear(); emit changed(); return; }
        if (command == "get_capabilities" && !taskId_.isEmpty())
            syncId_ = send("get_task_status", {{"target_request_id", taskId_}}, 3000);
        else if (command == "get_capabilities" || command == "get_task_status")
            syncId_ = send("get_scene", {}, 3000);
        else finishSync();
    }
    emit changed();
}
bool PlanningClient::applyEvent(const QJsonObject &e, bool snapshot) {
    if (taskId_.isEmpty() || e.value("request_id") != taskId_ || e.value("command") != taskCommand_) return false;
    const double sequence = e.value("event_seq").toDouble(-1);
    if (!std::isfinite(sequence) || sequence < (snapshot ? 0 : 1) || sequence > 9007199254740991.0
        || std::floor(sequence) != sequence || sequence < seq_) return false;
    if (sequence == seq_ && (!snapshot || (!taskTerminal_ && seq_ != 0))) {
        // A status snapshot at the same sequence may confirm an ongoing task.
        return snapshot && e.value("status") == "planning" && !taskTerminal_;
    }
    if (taskTerminal_) return snapshot && sequence == seq_ && e.value("status") == lastStatus_;
    const auto status = e.value("status").toString();
    if (status != "planning" && status != "succeeded" && status != "failed" && status != "cancelled") return false;
    if (sequence == 0 && status != "planning") return false;
    seq_ = qint64(sequence);
    lastStatus_ = status;
    if (status == "planning") setState(cancelRequested_ ? State::Cancelling : State::Planning);
    else {
        taskTerminal_ = true; pending_.remove(taskId_); ++taskGeneration_;
        if (status == "cancelled") setState(State::Cancelled);
        else if (status == "failed") { reportError(e); setState(State::Failed); }
        else if (cancelRequested_) {
            result_ = {}; setState(State::Failed);
            emit message("Cancel race: worker đã kết thúc nhưng trả succeeded; không công bố plan, chưa xác nhận cancelled.");
        } else {
            const auto p = e.value("payload").toObject();
            const auto validation = p.value("validation").toObject();
            const bool metadata = !p.value("plan_id").toString().isEmpty()
                && p.value("scene_revision") == taskRevision_ && p.value("robot_model_id") == taskModel_
                && p.value("group") == taskGroup_ && p.value("frame") == taskFrame_ && p.value("tcp_frame") == taskTcp_
                && p.value("scene_revision") == scene_.value("scene_revision")
                && p.value("robot_model_id") == capabilities_.value("robot_model_id")
                && p.value("duration_s").isDouble() && p.value("duration_s").toDouble() > 0
                && p.value("planning_time_s").isDouble() && p.value("planning_time_s").toDouble() >= 0
                && p.value("waypoint_count").toInt() > 0 && !p.value("joint_names").toArray().isEmpty()
                && !p.value("stages").toArray().isEmpty() && !validation.isEmpty();
            const bool mock = capabilities_.value("backend_mode") == "mock";
            const bool checks = mock ? validation.value("simulated") == QJsonValue(true)
                && validation.value("joint_limits") == "not_checked" && validation.value("collision") == "not_checked"
                && validation.value("timing") == "not_checked"
                : validation.value("simulated") == QJsonValue(false)
                && validation.value("joint_limits") == "passed" && validation.value("collision") == "passed"
                && validation.value("timing") == "passed";
            if (metadata && checks) {
                result_ = p; setState(State::PlanReady);
                const QString expectedPlan = planId();
                const int ttl = int(qMin(86400.0, capabilities_.value("plan_ttl_s").toDouble()) * 1000);
                QTimer::singleShot(ttl, this, [this, expectedPlan] { if (planId() == expectedPlan) invalidatePlan(); });
            } else { result_ = {}; setState(State::Failed); emit message("STALE_PLAN/VALIDATION_FAILED: metadata hoặc validation không hợp lệ."); }
        }
    }
    emit eventReceived(e); return true;
}
