#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>

enum class RobotComponent
{
    Power,
    Servo,
    Stream
};

enum class ComponentState
{
    Unknown,
    Off,
    On,
    PendingOn,
    PendingOff
};

struct ComponentStatus
{
    ComponentState state{ComponentState::Unknown};
    QString source;
    bool legacy{false};
};

struct ParsedSystemStatus
{
    bool accepted{false};
    bool canonical{false};
    bool robotConnected{true};
    ComponentStatus power;
    ComponentStatus servo;
    ComponentStatus stream;
    QStringList diagnostics;
};

struct ComponentViewState
{
    ComponentState state{ComponentState::Unknown};
    ComponentState confirmedState{ComponentState::Unknown};
    QString source;
    bool legacy{false};
};

struct SystemConfigurationView
{
    ComponentViewState power;
    ComponentViewState servo;
    ComponentViewState stream;
};

[[nodiscard]] ParsedSystemStatus parseSystemStatus(
    const QJsonObject &response);

[[nodiscard]] QString componentStateText(ComponentState state);

[[nodiscard]] bool isConfirmedComponentState(ComponentState state);
[[nodiscard]] bool isPendingComponentState(ComponentState state);

Q_DECLARE_METATYPE(ComponentState)
Q_DECLARE_METATYPE(ComponentViewState)
Q_DECLARE_METATYPE(SystemConfigurationView)
