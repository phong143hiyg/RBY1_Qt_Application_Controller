#include "state/ConnectedState.hpp"

#include "controller/RobotController.hpp"
#include "state/PreparingState.hpp"

std::unique_ptr<RobotState> ConnectedState::prepare(
    RobotController &controller)
{
    const bool sent =
        controller.sendSimpleInternal(
            QStringLiteral("prepare"),
            QStringLiteral("Chuẩn bị robot"));

    if (!sent)
    {
        return nullptr;
    }

    return std::make_unique<PreparingState>();
}
