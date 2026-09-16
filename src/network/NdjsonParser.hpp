#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QVector>

struct NdjsonFrame
{
    bool valid{false};
    QJsonObject object;
    QString error;
};

class NdjsonParser final
{
public:
    QVector<NdjsonFrame> append(const QByteArray &data);
    void clear();

    [[nodiscard]] QByteArray bufferedData() const;

private:
    QByteArray buffer_;
};
