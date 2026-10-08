# RBY1 direct-SDK migration

## Scope and pinned SDK

The Qt Widgets executable links the official Rainbow Robotics C++ SDK in the
same process. The vendored source is pinned to `v0.10.0`, commit
`9af8a734b7bef0167545e3d9f0d559276a5b64ee`; CMake checks
`rby1-sdk-main/UPSTREAM_REVISION` during configure. A normalized comparison
against the pinned checkout found two local differences: the Windows/MinGW
thread-name compatibility patch and extra cleanup in example 20. The example
is not built and neither difference alters the public robot API.

## Architecture

Before:

```text
Qt UI -> TCP/NDJSON robot bridge (:8081) -> ROS 2 driver -> robot
```

After:

```text
Qt Widgets
  -> RobotController / State Pattern
  -> IRby1Client (queued asynchronous contract)
  -> SdkRobotClient (one QThread owns every SDK object)
  -> rby1-sdk v0.10.0
  -> gRPC
  -> RB-Y1 or MuJoCo simulator (:50051)
```

Planning preview remains deliberately separate:

```text
PlanningPanel -> PlanningClient -> TCP/NDJSON (:8082) -> planning-only service
```

That path has no robot-execution command and must never own robot control.

## Legacy command mapping

| Legacy bridge command | Direct SDK/application implementation | v0.10.0 evidence |
|---|---|---|
| `ping` | `Robot::GetRobotInfo()` | `robot.h`, `01_hello_rby1.cpp` |
| `status` | `GetState()`, `GetControlManagerState()`, `IsPowerOn()`, `IsServoOn()` | `robot.h`, `robot_state.h`, `03_robot_state.cpp` |
| `prepare` | ordered application workflow: `PowerOn()` -> `ServoOn()` -> `EnableControlManager(false)`, each confirmed by subsequent state reads | `07_power.cpp`, `23_zero_pose.cpp` |
| `power` | `PowerOn()` / safe shutdown then `PowerOff()` | `robot.h`, `07_power.cpp` |
| `servo` | `ServoOn()` / `ServoOff()`; disabling first releases Control Manager | `robot.h`, `23_zero_pose.cpp` |
| `stream` | UI compatibility label for the real Control Manager lifecycle: `EnableControlManager(false)` / `DisableControlManager()` | `robot.h`, `30_joint_group_command.cpp` |
| `velocity` | lazily create `CreateCommandStream()`, then send `SE2VelocityCommandBuilder` | `robot.h`, `32_command_stream.cpp`, `37_mobile_test.cpp` |
| `stop` | send zero SE(2) velocity, cancel and destroy the command stream | `32_command_stream.cpp`, `37_mobile_test.cpp` |
| `cancel` | cancel active command/stream and call `CancelControl()` | `26_cancel_control.cpp` |
| `joints_status` | `GetState().position` indexed with `RobotInfo` group indices | `robot_state.h`, `model.h`, `03_robot_state.cpp` |
| `set_ready_pose` | application-owned, validated upper-body snapshot (torso/head/two arms) | application layer; no equivalent persistent SDK slot |
| `clear_ready_pose` | clear the application-owned snapshot | application layer |
| `ready_pose` | send the saved upper-body component command | `23_zero_pose.cpp`, `30_joint_group_command.cpp` |
| `arms_ready` | send the documented folded-arm target as right/left arm component commands | component-command API in `30_joint_group_command.cpp` |
| `zero_pose` | send zero torso and arm/head component targets | `23_zero_pose.cpp` |

The SDK has command streams, but no ROS-driver-style global “stream ON/OFF”.
Consequently the UI's former Stream switch represents Control Manager enablement;
the mobility command stream exists only while base velocity is being commanded.

## Threading and safety

- `SdkRobotClient::Impl` owns `Robot<Model>`, command handles and streams on one
  worker `QThread`. UI calls enqueue work; results are queued back to Qt.
- A command has a finite timer. Timeout invalidates the request and cancels an
  active motion handle.
- Base commands use a 0.30 s SDK control-hold time and are refreshed at 100 ms.
  Button release sends zero immediately. Disconnect/destruction also attempts
  zero velocity and `CancelControl()` before destroying the SDK object.
- Joint motion is serialized. Completion requires both successful gRPC status
  and SDK `finish_code == kOk`; an RPC return alone is not reported as success.
- Status older than the controller deadline makes Power, Servo and Control
  Manager `UNKNOWN`, exits Ready, and blocks new motion.
- Power-off cancels motion, sends base zero, releases Control Manager, turns
  servos off, then calls `PowerOff()`.
- Startup never powers or servos the robot automatically.

## Simulator-before-hardware procedure

1. Configure the simulator so its gRPC SDK endpoint is published directly as
   `127.0.0.1:50051`. Do not start a ROS driver or TCP relay.
2. Run `tools/start-rby1-sim.ps1`, then set `RBY1_TEST_ROBOT_ADDRESS` and
   `RBY1_TEST_ROBOT_MODEL=m` (or `a`).
3. Run the read-only SDK integration test first. It connects, reads robot info,
   validates the model layout, reads state and disconnects.
4. Motion integration tests are opt-in through
   `RBY1_TEST_ENABLE_MOTION=1`. Use only the simulator, verify zero velocity,
   cancel and reconnect, then test a small bounded joint move.
5. Before using hardware, verify the endpoint/model, clear the work area,
   confirm E-stop operation, use reduced limits and keep motion tests opt-in.

## Remaining limitations

- CI has no simulator/physical robot, so live gRPC, power/servo and motion
  integration results must not be inferred from unit-test success.
- Ready pose is session-local and is intentionally not written to the robot.
- The independent planning service may use ROS 2 internally, but this repository
  neither builds nor runs ROS 2 for robot control and planning cannot execute.
- The vendored SDK contains a small MinGW compatibility patch; upgrading SDK or
  compiler requires a fresh ABI/build verification.
