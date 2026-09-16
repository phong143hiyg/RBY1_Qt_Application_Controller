#include "model/SystemStatus.hpp"

#include <QJsonValue>
#include <QStringList>

namespace
{
ComponentStatus unknownStatus(
    const QString &source = {},
    bool legacy = false)
{
    return {
        ComponentState::Unknown,
        source,
        legacy
    };
}

ComponentStatus parseCanonicalComponent(
    const QJsonObject &components,
    const QString &name,
    QStringList &diagnostics)
{
    const QJsonValue value = components.value(name);

    if (!value.isObject())
    {
        diagnostics.push_back(
            QStringLiteral("components.%1 is missing or is not an object")
                .arg(name));
        return unknownStatus();
    }

    const QJsonObject object = value.toObject();
    const QJsonValue knownValue = object.value(QStringLiteral("known"));
    const QString source =
        object.value(QStringLiteral("source")).isString()
            ? object.value(QStringLiteral("source")).toString()
            : QStringLiteral("canonical");

    if (!knownValue.isBool())
    {
        diagnostics.push_back(
            QStringLiteral("components.%1.known is missing or is not boolean")
                .arg(name));
        return unknownStatus(source);
    }

    if (!knownValue.toBool())
    {
        return unknownStatus(source);
    }

    const QJsonValue enabledValue =
        object.value(QStringLiteral("enabled"));
    const QJsonValue pendingValue =
        object.value(QStringLiteral("pending"));

    if (!enabledValue.isBool())
    {
        diagnostics.push_back(
            QStringLiteral("components.%1.enabled is missing or is not boolean")
                .arg(name));
        return unknownStatus(source);
    }

    if (!pendingValue.isBool())
    {
        diagnostics.push_back(
            QStringLiteral("components.%1.pending is missing or is not boolean")
                .arg(name));
        return unknownStatus(source);
    }

    const bool enabled = enabledValue.toBool();
    const bool pending = pendingValue.toBool();

    return {
        pending
            ? enabled
                ? ComponentState::PendingOn
                : ComponentState::PendingOff
            : enabled
                ? ComponentState::On
                : ComponentState::Off,
        source,
        false
    };
}

QJsonValue findLegacyValue(
    const QJsonObject &response,
    const QJsonObject &status,
    const QStringList &keys)
{
    for (const QString &key : keys)
    {
        if (response.contains(key))
        {
            return response.value(key);
        }

        if (status.contains(key))
        {
            return status.value(key);
        }
    }

    return {};
}

ComponentStatus parseLegacyComponent(
    const QJsonObject &response,
    const QJsonObject &status,
    const QString &name,
    const QStringList &keys,
    QStringList &diagnostics)
{
    const QJsonValue value = findLegacyValue(response, status, keys);

    if (!value.isBool())
    {
        diagnostics.push_back(
            QStringLiteral("legacy %1 field is missing or is not boolean")
                .arg(name));
        return unknownStatus(QStringLiteral("legacy"), true);
    }

    return {
        value.toBool() ? ComponentState::On : ComponentState::Off,
        QStringLiteral("legacy"),
        true
    };
}
}

ParsedSystemStatus parseSystemStatus(
    const QJsonObject &response)
{
    ParsedSystemStatus result;

    if (!response.value(QStringLiteral("success")).isBool()
        || !response.value(QStringLiteral("success")).toBool())
    {
        result.diagnostics.push_back(
            QStringLiteral("status response does not contain success=true"));
        return result;
    }

    result.accepted = true;

    if (response.value(QStringLiteral("connected")).isBool())
    {
        result.bridgeConnected =
            response.value(QStringLiteral("connected")).toBool();
    }

    const QJsonObject status =
        response.value(QStringLiteral("status")).isObject()
            ? response.value(QStringLiteral("status")).toObject()
            : QJsonObject{};

    const bool hasRootComponents =
        response.contains(QStringLiteral("components"));
    const bool hasNestedComponents =
        status.contains(QStringLiteral("components"));

    if (hasRootComponents || hasNestedComponents)
    {
        result.canonical = true;

        const QJsonValue componentsValue = hasRootComponents
            ? response.value(QStringLiteral("components"))
            : status.value(QStringLiteral("components"));

        if (!componentsValue.isObject())
        {
            result.diagnostics.push_back(
                QStringLiteral("components is present but is not an object"));
            return result;
        }

        const QJsonObject components = componentsValue.toObject();

        result.power = parseCanonicalComponent(
            components, QStringLiteral("power"), result.diagnostics);
        result.servo = parseCanonicalComponent(
            components, QStringLiteral("servo"), result.diagnostics);
        result.stream = parseCanonicalComponent(
            components, QStringLiteral("stream"), result.diagnostics);
        return result;
    }

    result.power = parseLegacyComponent(
        response,
        status,
        QStringLiteral("power"),
        {
            QStringLiteral("power"),
            QStringLiteral("power_on"),
            QStringLiteral("powered")
        },
        result.diagnostics);

    result.servo = parseLegacyComponent(
        response,
        status,
        QStringLiteral("servo"),
        {
            QStringLiteral("servo"),
            QStringLiteral("servo_on"),
            QStringLiteral("servo_enabled")
        },
        result.diagnostics);

    result.stream = parseLegacyComponent(
        response,
        status,
        QStringLiteral("stream"),
        {
            QStringLiteral("stream"),
            QStringLiteral("stream_on"),
            QStringLiteral("streaming"),
            QStringLiteral("stream_enabled")
        },
        result.diagnostics);

    return result;
}

QString componentStateText(ComponentState state)
{
    switch (state)
    {
    case ComponentState::Off:
        return QStringLiteral("OFF");
    case ComponentState::On:
        return QStringLiteral("ON");
    case ComponentState::PendingOn:
        return QStringLiteral("TURNING ON\u2026");
    case ComponentState::PendingOff:
        return QStringLiteral("TURNING OFF\u2026");
    case ComponentState::Unknown:
    default:
        return QStringLiteral("UNKNOWN");
    }
}

bool isConfirmedComponentState(ComponentState state)
{
    return state == ComponentState::Off || state == ComponentState::On;
}

bool isPendingComponentState(ComponentState state)
{
    return state == ComponentState::PendingOn
        || state == ComponentState::PendingOff;
}
