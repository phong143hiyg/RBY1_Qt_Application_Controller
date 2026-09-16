#pragma once

#include "network/NdjsonParser.hpp"

#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTcpSocket>

class RobotClient final : public QObject
{
    Q_OBJECT

public:
    explicit RobotClient(QObject *parent = nullptr);
    ~RobotClient() override;

    void connectToBridge(
        const QString &host = QStringLiteral("127.0.0.1"),
        quint16 port = 8081);

    void disconnectFromBridge();

    [[nodiscard]] bool isConnected() const;

    // Returns a monotonically increasing local request id, or zero if the
    // command could not be queued. The bridge wire schema is unchanged.
    quint64 sendCommand(
        const QJsonObject &command,
        const QString &operationName,
        int timeoutMs = 3000);

signals:
    void bridgeConnected();
    void bridgeDisconnected();

    void responseReceived(
        quint64 requestId,
        const QString &operationName,
        const QJsonObject &response);

    void requestTimedOut(
        quint64 requestId,
        const QString &operationName);

    void clientError(const QString &message);

private:
    struct PendingRequest
    {
        quint64 id{0};
        QString operationName;
        QString commandName;
        bool timeoutEmitted{false};
    };

    void processFrames(const QVector<NdjsonFrame> &frames);

    QTcpSocket socket_;
    NdjsonParser parser_;
    QQueue<PendingRequest> pendingRequests_;
    quint64 nextRequestId_{1};
};
