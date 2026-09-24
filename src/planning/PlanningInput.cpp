#include "planning/PlanningInput.hpp"
#include <QJsonArray>
#include <cmath>

namespace {
bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
bool number(const QJsonValue &v) { return v.isDouble() && std::isfinite(v.toDouble()); }
}
bool PlanningInput::pose(const QJsonObject &p, QString *error) {
    if (p.value("frame_id").toString().trimmed().isEmpty())
        return fail(error, QStringLiteral("Pose cần frame_id."));
    const auto pos = p.value("position").toArray();
    const auto q = p.value("orientation_xyzw").toArray();
    if (pos.size() != 3 || q.size() != 4)
        return fail(error, QStringLiteral("Position [x,y,z] m; quaternion [x,y,z,w]."));
    for (const auto &v : pos) if (!number(v)) return fail(error, "Position phải hữu hạn.");
    double norm = 0;
    for (const auto &v : q) {
        if (!number(v)) return fail(error, "Quaternion phải hữu hạn.");
        norm += v.toDouble() * v.toDouble();
    }
    if (!std::isfinite(norm) || std::abs(norm - 1.0) > 0.002001)
        return fail(error, "Quaternion phải có norm = 1 (sai số 0.001); không tự chuẩn hóa.");
    return true;
}
bool PlanningInput::request(const QString &command, const QJsonObject &p, double maxTimeout,
                            QString *error) {
    if (command != "plan_to_pose" && command != "plan_pick_place")
        return fail(error, "Unsupported planning command.");
    for (const auto &key : {"scene_revision", "group", "tcp_frame"})
        if (p.value(key).toString().isEmpty()) return fail(error, QString("Thiếu %1.").arg(key));
    for (const auto &key : {"velocity_scale", "acceleration_scale"})
        if (!number(p.value(key)) || p.value(key).toDouble() <= 0 || p.value(key).toDouble() > 1)
            return fail(error, QString("%1 phải trong (0,1].").arg(key));
    const auto timeout = p.value("planning_timeout_s");
    if (!number(timeout) || timeout.toDouble() <= 0 || timeout.toDouble() > maxTimeout)
        return fail(error, QString("Timeout phải trong (0,%1] s.").arg(maxTimeout));
    if (command == "plan_to_pose") return pose(p.value("goal_tcp_pose").toObject(), error);
    if (p.value("object_id").toString().isEmpty()) return fail(error, "Thiếu object_id.");
    for (const auto &key : {"approach_distance_m", "lift_distance_m", "retreat_distance_m"})
        if (!number(p.value(key)) || p.value(key).toDouble() < 0)
            return fail(error, QString("%1 phải hữu hạn và không âm.").arg(key));
    return pose(p.value("pick_tcp_pose").toObject(), error)
        && pose(p.value("place_object_pose").toObject(), error);
}
