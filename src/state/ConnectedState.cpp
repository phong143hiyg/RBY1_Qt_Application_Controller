#include "state/ConnectedState.hpp"

#include "controller/RobotController.hpp"
#include "state/PreparingState.hpp"

std::unique_ptr<RobotState> ConnectedState::prepare(
    RobotController &controller)
{
    auto preparing = std::make_unique<PreparingState>();

    if (!preparing->start(controller))
    {
        return nullptr;
    }

    return preparing;
}
