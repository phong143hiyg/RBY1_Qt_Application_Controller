#include "state/PreparingState.hpp"

#include "controller/RobotController.hpp"
#include "state/ConnectedState.hpp"
#include "state/ReadyState.hpp"

bool PreparingState::start(RobotController &controller)
{
    return controller.beginPreparationInternal();
}

std::unique_ptr<RobotState> PreparingState::onResponse(
    RobotController &controller,
    quint64 requestId,
    const QString &operationName,
    const QJsonObject &response)
{
    Q_UNUSED(controller)
    Q_UNUSED(requestId)
    Q_UNUSED(operationName)
    Q_UNUSED(response)

    // RobotController advances preparation only after status.components
    // confirms each step. Command acknowledgements never advance this state.
    return nullptr;
}
