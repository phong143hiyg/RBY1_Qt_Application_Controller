#include "network/NdjsonParser.hpp"

#include <QJsonDocument>
#include <QJsonParseError>

QVector<NdjsonFrame> NdjsonParser::append(const QByteArray &data)
{
    QVector<NdjsonFrame> result;
    QByteArray incoming = data;
    if (discarding_)
    {
        const auto end = incoming.indexOf('\n');
        if (end < 0) return result;
        incoming.remove(0, end + 1);
        discarding_ = false;
    }
    buffer_.append(incoming);

    while (true)
    {
        const qsizetype newlineIndex = buffer_.indexOf('\n');

        if (newlineIndex < 0)
        {
            if (maxFrameBytes_ > 0 && buffer_.size() > maxFrameBytes_)
            {
                buffer_.clear();
                discarding_ = true;
                result.push_back({false, {}, QStringLiteral("NDJSON frame exceeds limit")});
            }
            return result;
        }

        if (maxFrameBytes_ > 0 && newlineIndex > maxFrameBytes_)
        {
            buffer_.remove(0, newlineIndex + 1);
            result.push_back({false, {}, QStringLiteral("NDJSON frame exceeds limit")});
            continue;
        }

        const QByteArray line = buffer_.left(newlineIndex).trimmed();
        buffer_.remove(0, newlineIndex + 1);

        if (line.isEmpty())
        {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(line, &parseError);

        if (parseError.error != QJsonParseError::NoError
            || !document.isObject())
        {
            result.push_back({
                false,
                {},
                QStringLiteral("Invalid NDJSON frame: %1")
                    .arg(parseError.errorString())
            });
            continue;
        }

        result.push_back({true, document.object(), {}});
    }
}

void NdjsonParser::clear()
{
    buffer_.clear();
    discarding_ = false;
}

QByteArray NdjsonParser::bufferedData() const
{
    return buffer_;
}
