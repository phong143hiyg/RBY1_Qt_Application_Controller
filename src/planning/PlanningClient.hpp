#pragma once
#include "network/NdjsonParser.hpp"
#include <QObject>
#include <QTcpSocket>
#include <QHash>
#include <QTimer>

// Independent planning transport. Nothing is routed through RobotController/RobotState.
class PlanningClient final : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Planning, PlanReady, Failed, Cancelling, Cancelled, Disconnected };
    Q_ENUM(State)
    explicit PlanningClient(QObject *parent = nullptr);
    ~PlanningClient() override;
    void connectToServer(const QString &host, quint16 port = 8082);
    void disconnectFromServer();
    void reconcile();
    QString loadScene(const QString &scenario);
    QString plan(const QString &command, const QJsonObject &payload);
    QString cancel();
    QString preview();
    // Read-only queries, also used for explicit refresh and protocol correlation tests.
    QString query(const QString &command, const QJsonObject &payload = {}, int timeoutMs = 3000);
    State state() const { return state_; }
    bool synchronized() const { return synced_; }
    bool busy() const { return !taskTerminal_ && !taskId_.isEmpty(); }
    bool canStart() const;
    bool supports(const QString &command) const;
    QString taskId() const { return taskId_; }
    QString planId() const { return result_.value("plan_id").toString(); }
    QJsonObject capabilities() const { return capabilities_; }
    QJsonObject scene() const { return scene_; }
    QJsonObject result() const { return result_; }
signals:
    void changed();
    void message(const QString &text);
    void responseReceived(const QString &id, const QString &command, const QJsonObject &envelope);
    void eventReceived(const QJsonObject &envelope);
    void requestTimedOut(const QString &id, const QString &command);
private:
    struct Pending { QString command; QJsonObject payload; quint64 serial{0}; };
    QString send(const QString &command, const QJsonObject &payload, int timeoutMs);
    void receive(const QJsonObject &envelope);
    bool applyEvent(const QJsonObject &envelope, bool snapshot = false);
    bool applyCapabilities(const QJsonObject &payload);
    bool applyScene(const QJsonObject &payload);
    void invalidatePlan();
    void setState(State value);
    void finishSync();
    void reportError(const QJsonObject &envelope);
    QTcpSocket socket_;
    NdjsonParser parser_{1024 * 1024};
    QHash<QString, Pending> pending_;
    QTimer refresh_;
    QString session_, taskId_, taskCommand_, syncId_, sceneMutationId_, lastStatus_;
    QString taskRevision_, taskModel_, taskGroup_, taskTcp_, taskFrame_;
    quint64 counter_{0};
    quint64 latestSceneSerial_{0}, latestCapabilitySerial_{0};
    qint64 seq_{0};
    quint64 taskGeneration_{0};
    State state_{State::Disconnected};
    bool synced_{false}, reconciling_{false}, taskTerminal_{true};
    bool cancelRequested_{false}, uncertain_{false};
    QJsonObject capabilities_, scene_, result_;
};
