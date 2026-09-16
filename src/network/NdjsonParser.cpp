#include "network/NdjsonParser.hpp"

#include <QJsonDocument>
#include <QJsonParseError>

QVector<NdjsonFrame> NdjsonParser::append(const QByteArray &data)
{
    buffer_.append(data);
    QVector<NdjsonFrame> result;

    while (true)
    {
        const qsizetype newlineIndex = buffer_.indexOf('\n');

        if (newlineIndex < 0)
        {
            return result;
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
                QStringLiteral("Invalid bridge JSON: %1")
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
}

QByteArray NdjsonParser::bufferedData() const
{
    return buffer_;
}
