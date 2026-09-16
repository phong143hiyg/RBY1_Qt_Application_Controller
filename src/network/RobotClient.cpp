#include "network/RobotClient.hpp"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QStringList>
#include <QTimer>

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
            parser_.clear();
            pendingRequests_.clear();
            emit bridgeDisconnected();
        });

    connect(
        &socket_,
        &QTcpSocket::readyRead,
        this,
        [this]()
        {
            processFrames(parser_.append(socket_.readAll()));
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

RobotClient::~RobotClient()
{
    socket_.disconnect(this);
    socket_.abort();
    pendingRequests_.clear();
    parser_.clear();
}

void RobotClient::connectToBridge(
    const QString &host,
    quint16 port)
{
    if (socket_.state() != QAbstractSocket::UnconnectedState)
    {
        socket_.abort();
    }

    parser_.clear();
    pendingRequests_.clear();
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

quint64 RobotClient::sendCommand(
    const QJsonObject &command,
    const QString &operationName,
    int timeoutMs)
{
    if (!isConnected())
    {
        emit clientError(
            QStringLiteral("The application is not connected to App Bridge."));
        return 0;
    }

    QByteArray payload =
        QJsonDocument(command).toJson(QJsonDocument::Compact);
    payload.append('\n');

    const quint64 requestId = nextRequestId_++;
    pendingRequests_.enqueue({
        requestId,
        operationName,
        command.value(QStringLiteral("command")).toString(),
        false
    });

    if (socket_.write(payload) < 0)
    {
        pendingRequests_.removeLast();
        emit clientError(socket_.errorString());
        return 0;
    }

    if (timeoutMs > 0)
    {
        QTimer::singleShot(
            timeoutMs,
            this,
            [this, requestId]()
            {
                for (PendingRequest &request : pendingRequests_)
                {
                    if (request.id != requestId || request.timeoutEmitted)
                    {
                        continue;
                    }

                    request.timeoutEmitted = true;
                    emit requestTimedOut(
                        request.id,
                        request.operationName);
                    break;
                }
            });
    }

    return requestId;
}

void RobotClient::processFrames(
    const QVector<NdjsonFrame> &frames)
{
    for (const NdjsonFrame &frame : frames)
    {
        if (!frame.valid)
        {
            if (!pendingRequests_.isEmpty())
            {
                pendingRequests_.dequeue();
            }
            emit clientError(frame.error);
            continue;
        }

        const QJsonObject nestedStatus =
            frame.object.value(QStringLiteral("status")).isObject()
                ? frame.object.value(QStringLiteral("status")).toObject()
                : QJsonObject{};
        const bool isCanonicalStatus =
            frame.object.contains(QStringLiteral("components"))
            || nestedStatus.contains(QStringLiteral("components"));
        const bool isJointSnapshot =
            frame.object.value(QStringLiteral("groups")).isObject()
            || frame.object.contains(QStringLiteral("torso"))
            || frame.object.contains(QStringLiteral("head"))
            || frame.object.contains(QStringLiteral("right_arm"))
            || frame.object.contains(QStringLiteral("left_arm"));

        PendingRequest request{
            0,
            isCanonicalStatus
                ? QStringLiteral("Status")
                : QStringLiteral("Unsolicited response"),
            {},
            false
        };

        if (isCanonicalStatus)
        {
            for (qsizetype index = 0;
                 index < pendingRequests_.size();
                 ++index)
            {
                if (pendingRequests_.at(index).operationName
                    == QStringLiteral("Status"))
                {
                    request = pendingRequests_.takeAt(index);
                    break;
                }
            }
        }
        else if (isJointSnapshot)
        {
            request.operationName = QStringLiteral("Joints status");
            request.commandName = QStringLiteral("joints_status");

            for (qsizetype index = 0;
                 index < pendingRequests_.size();
                 ++index)
            {
                if (pendingRequests_.at(index).commandName
                    == QStringLiteral("joints_status"))
                {
                    request = pendingRequests_.takeAt(index);
                    break;
                }
            }
        }
        else if (!pendingRequests_.isEmpty())
        {
            QString echoedCommand;
            const QStringList commandKeys{
                QStringLiteral("command"),
                QStringLiteral("request_command")
            };

            for (const QString &key : commandKeys)
            {
                if (frame.object.value(key).isString())
                {
                    echoedCommand = frame.object.value(key).toString();
                    break;
                }
            }

            qsizetype matchedIndex = -1;

            if (!echoedCommand.isEmpty())
            {
                for (qsizetype index = 0;
                     index < pendingRequests_.size();
                     ++index)
                {
                    if (pendingRequests_.at(index).commandName
                        == echoedCommand)
                    {
                        matchedIndex = index;
                        break;
                    }
                }
            }

            // A canonical status response is self-identifying. If a plain
            // command ACK arrives while Status is at the head of the queue,
            // associate it with the first non-status request instead of
            // consuming the Status slot and permanently stranding JointBusy.
            const auto isBackgroundRequest =
                [](const PendingRequest &pending)
                {
                    return pending.commandName == QStringLiteral("status")
                        || pending.commandName
                            == QStringLiteral("joints_status");
                };

            if (matchedIndex < 0
                && isBackgroundRequest(pendingRequests_.head()))
            {
                for (qsizetype index = 1;
                     index < pendingRequests_.size();
                     ++index)
                {
                    if (!isBackgroundRequest(pendingRequests_.at(index)))
                    {
                        matchedIndex = index;
                        break;
                    }
                }
            }

            request = matchedIndex >= 0
                ? pendingRequests_.takeAt(matchedIndex)
                : pendingRequests_.dequeue();
        }

        emit responseReceived(
            request.id,
            request.operationName,
            frame.object);
    }
}
