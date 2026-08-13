#pragma once

#include <QByteArray>
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

    void connectToBridge(
        const QString &host = QStringLiteral("127.0.0.1"),
        quint16 port = 8081);

    void disconnectFromBridge();

    [[nodiscard]] bool isConnected() const;

    bool sendCommand(
        const QJsonObject &command,
        const QString &operationName);

signals:
    void bridgeConnected();
    void bridgeDisconnected();

    void responseReceived(
        const QString &operationName,
        const QJsonObject &response);

    void clientError(const QString &message);

private:
    void processIncomingLines();

    QTcpSocket socket_;
    QByteArray receiveBuffer_;
    QQueue<QString> pendingOperations_;
};
