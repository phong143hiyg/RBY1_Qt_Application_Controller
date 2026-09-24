#pragma once
#include <QJsonObject>
#include <QString>

namespace PlanningInput {
bool pose(const QJsonObject &value, QString *error = nullptr);
bool request(const QString &command, const QJsonObject &payload, double maxTimeout,
             QString *error = nullptr);
}
