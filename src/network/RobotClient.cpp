#include "network/RobotClient.hpp"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonParseError>

RobotClient::RobotClient(QObject *parent)
    : QObject(parent)
{
    connect(
        &socket_,
        &QTcpSocket::connected,
        this,
        &RobotClient::bridgeConnected);

    connect(
        &socket_,
        &QTcpSocket::disconnected,
        this,
        [this]()
        {
            receiveBuffer_.clear();
            pendingOperations_.clear();
            emit bridgeDisconnected();
        });

    connect(
        &socket_,
        &QTcpSocket::readyRead,
        this,
        [this]()
        {
            receiveBuffer_.append(socket_.readAll());
            processIncomingLines();
        });

    connect(
        &socket_,
        &QTcpSocket::errorOccurred,
        this,
        [this](QAbstractSocket::SocketError)
        {
            emit clientError(socket_.errorString());
        });
}

void RobotClient::connectToBridge(
    const QString &host,
    quint16 port)
{
    if (socket_.state() != QAbstractSocket::UnconnectedState)
    {
        socket_.abort();
    }

    receiveBuffer_.clear();
    pendingOperations_.clear();

    socket_.connectToHost(host, port);
}

void RobotClient::disconnectFromBridge()
{
    if (socket_.state() == QAbstractSocket::UnconnectedState)
    {
        return;
    }

    socket_.disconnectFromHost();

    if (socket_.state() != QAbstractSocket::UnconnectedState)
    {
        socket_.waitForDisconnected(500);
    }
}

bool RobotClient::isConnected() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

bool RobotClient::sendCommand(
    const QJsonObject &command,
    const QString &operationName)
{
    if (!isConnected())
    {
        emit clientError(
            QStringLiteral(
                "Ứng dụng chưa kết nối C++ ROS 2 bridge."));
        return false;
    }

    QByteArray payload =
        QJsonDocument(command).toJson(
            QJsonDocument::Compact);

    payload.append('\n');

    pendingOperations_.enqueue(operationName);

    if (socket_.write(payload) < 0)
    {
        if (!pendingOperations_.isEmpty())
        {
            pendingOperations_.dequeue();
        }

        emit clientError(socket_.errorString());
        return false;
    }

    return true;
}

void RobotClient::processIncomingLines()
{
    while (true)
    {
        const qsizetype newlineIndex =
            receiveBuffer_.indexOf('\n');

        if (newlineIndex < 0)
        {
            return;
        }

        QByteArray line =
            receiveBuffer_.left(newlineIndex).trimmed();

        receiveBuffer_.remove(
            0,
            newlineIndex + 1);

        if (line.isEmpty())
        {
            continue;
        }

        const QString operationName =
            pendingOperations_.isEmpty()
                ? QStringLiteral("Phản hồi")
                : pendingOperations_.dequeue();

        QJsonParseError parseError;

        const QJsonDocument document =
            QJsonDocument::fromJson(
                line,
                &parseError);

        if (
            parseError.error != QJsonParseError::NoError
            || !document.isObject())
        {
            emit clientError(
                QStringLiteral(
                    "Bridge trả JSON không hợp lệ: %1")
                    .arg(parseError.errorString()));
            continue;
        }

        emit responseReceived(
            operationName,
            document.object());
    }
}
