#include "state/JointBusyState.hpp"

#include "controller/RobotController.hpp"
#include "state/ReadyState.hpp"

#include <QJsonArray>
#include <QtMath>

namespace
{
constexpr double kMaxNudgeDeltaRadians = 0.20;
constexpr double kTargetToleranceRadians = 0.05 * 3.14159265358979323846 / 180.0;
}

JointBusyState::JointBusyState(quint64 snapshotRequestId, QString groupName,
                             int jointIndex, double targetRadians, double minimumTime)
    : pendingOperation_(QStringLiteral("Joints status")),
      pendingRequestId_(snapshotRequestId), groupName_(std::move(groupName)),
      jointIndex_(jointIndex), absoluteTarget_(true), targetRadians_(targetRadians),
      requestedMinimumTime_(minimumTime)
{
}

JointBusyState::JointBusyState(
    QString pendingOperation,
    quint64 pendingRequestId)
    : pendingOperation_(
          std::move(pendingOperation)),
      pendingRequestId_(pendingRequestId)
{
}

JointBusyState::JointBusyState(
    QString pendingOperation,
    quint64 pendingRequestId,
    QString groupName,
    int jointIndex,
    double remainingDelta,
    double segmentMinimumTime)
    : pendingOperation_(
          std::move(pendingOperation)),
      pendingRequestId_(pendingRequestId),
      groupName_(
          std::move(groupName)),
      jointIndex_(jointIndex),
      remainingDelta_(remainingDelta),
      segmentMinimumTime_(segmentMinimumTime)
{
}

std::unique_ptr<RobotState> JointBusyState::onResponse(
    RobotController &controller,
    quint64 requestId,
    const QString &operationName,
    const QJsonObject &response)
{
    if (requestId != pendingRequestId_
        || operationName != pendingOperation_)
    {
        return nullptr;
    }

    const bool snapshotWithoutSuccess = absoluteTarget_
        && pendingOperation_ == QStringLiteral("Joints status")
        && !response.contains(QStringLiteral("success"));
    const bool success = snapshotWithoutSuccess
        || response.value(QStringLiteral("success")).toBool(false);

    if (!success)
    {
        if (absoluteTarget_ || pendingOperation_ == QStringLiteral("Joint nudge"))
        {
            QString reason = response.value(QStringLiteral("message")).toString();
            if (reason.isEmpty())
            {
                reason = response.value(QStringLiteral("error")).toString();
            }
            controller.reportJointMotionFailure(reason.isEmpty()
                ? QStringLiteral("Robot SDK từ chối lệnh thay đổi góc khớp.") : reason);
        }
        controller.appendStateLog(
            QStringLiteral("%1 failed; returning to Ready.")
                .arg(pendingOperation_));
        return std::make_unique<ReadyState>();
    }

    if (absoluteTarget_)
    {
        if (pendingOperation_ == QStringLiteral("Joint nudge"))
        {
            // An ACK is not a position measurement. Recompute the next
            // delta from fresh feedback instead of adding a fixed remainder.
            pendingOperation_ = QStringLiteral("Joints status");
            pendingRequestId_ = controller.requestJointSnapshotInternal();
        }
        else
        {
            const QJsonObject groups = response.value(QStringLiteral("groups")).isObject()
                ? response.value(QStringLiteral("groups")).toObject() : response;
            const QJsonValue group = groups.value(groupName_);
            const QJsonArray positions = group.isArray() ? group.toArray()
                : group.toObject().value(QStringLiteral("positions")).toArray();
            if (jointIndex_ < 0 || jointIndex_ >= positions.size() || !positions.at(jointIndex_).isDouble()
                || !qIsFinite(positions.at(jointIndex_).toDouble()))
            {
                controller.reportJointMotionFailure(QStringLiteral("Robot did not return a valid position for the requested joint."));
                return std::make_unique<ReadyState>();
            }
            const double delta = targetRadians_ - positions.at(jointIndex_).toDouble();
            if (qAbs(delta) <= kTargetToleranceRadians)
            {
                controller.appendStateLog(QStringLiteral("Joint target confirmed by robot feedback."));
                return std::make_unique<ReadyState>();
            }
            if (segmentsRemaining_ < 0)
            {
                const int count = qMax(1, qCeil(qAbs(delta) / kMaxNudgeDeltaRadians));
                segmentMinimumTime_ = qMax(0.20, requestedMinimumTime_ / count);
                // Bound corrections when feedback stalls or another client
                // changes the joint while this target is being executed.
                segmentsRemaining_ = count + 8;
            }
            if (segmentsRemaining_-- == 0)
            {
                controller.reportJointMotionFailure(QStringLiteral("Joint target was not reached; stopping further commands."));
                return std::make_unique<ReadyState>();
            }
            pendingOperation_ = QStringLiteral("Joint nudge");
            pendingRequestId_ = controller.sendJointNudgeInternal(groupName_, jointIndex_,
                qBound(-kMaxNudgeDeltaRadians, delta, kMaxNudgeDeltaRadians), segmentMinimumTime_);
        }
        if (pendingRequestId_ == 0)
        {
            controller.reportJointMotionFailure(QStringLiteral("Could not send the next joint motion request."));
            return std::make_unique<ReadyState>();
        }
        return nullptr;
    }

    if (remainingDelta_ > 1e-9 || remainingDelta_ < -1e-9)
    {
        const double nextDelta =
            remainingDelta_ > kMaxNudgeDeltaRadians
                ? kMaxNudgeDeltaRadians
                : remainingDelta_ < -kMaxNudgeDeltaRadians
                    ? -kMaxNudgeDeltaRadians
                    : remainingDelta_;

        const quint64 nextRequestId =
            controller.sendJointNudgeInternal(
                groupName_,
                jointIndex_,
                nextDelta,
                segmentMinimumTime_);

        if (nextRequestId != 0)
        {
            remainingDelta_ -= nextDelta;
            pendingRequestId_ = nextRequestId;
            return nullptr;
        }

        controller.appendStateLog(
            QStringLiteral("Could not send the next joint nudge; returning to Ready."));
        controller.reportJointMotionFailure(QStringLiteral("Không gửi được đoạn chuyển động khớp tiếp theo."));
        return std::make_unique<ReadyState>();
    }

    controller.appendStateLog(
        QStringLiteral("%1 completed; returning to Ready.")
            .arg(pendingOperation_));
    controller.scheduleJointRefresh();
    return std::make_unique<ReadyState>();
}

std::unique_ptr<RobotState> JointBusyState::onRequestTimeout(
    RobotController &controller,
    quint64 requestId,
    const QString &operationName)
{
    if (requestId != pendingRequestId_
        || operationName != pendingOperation_)
    {
        return nullptr;
    }

    controller.appendStateLog(
        QStringLiteral(
            "%1 timed out; releasing JointBusy and refreshing robot state.")
            .arg(pendingOperation_));
    if (absoluteTarget_ || pendingOperation_ == QStringLiteral("Joint nudge"))
    {
        controller.reportJointMotionFailure(QStringLiteral(
            "Hết thời gian chờ SDK xác nhận lệnh thay đổi góc khớp. Đang đọc lại trạng thái robot."));
    }
    controller.scheduleJointRefresh();
    return std::make_unique<ReadyState>();
}
