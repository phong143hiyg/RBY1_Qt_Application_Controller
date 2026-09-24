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
    explicit NdjsonParser(qsizetype maxFrameBytes = 0) : maxFrameBytes_(maxFrameBytes) {}
    QVector<NdjsonFrame> append(const QByteArray &data);
    void clear();

    [[nodiscard]] QByteArray bufferedData() const;

private:
    QByteArray buffer_;
    qsizetype maxFrameBytes_{0};
    bool discarding_{false};
};
