# RBY1 Desktop Qt — Mốc 4 dùng State Pattern

Bản này refactor Mốc 4 sang **State Pattern**.

## Kiến trúc

```text
MainWindow (View)
        |
        v
RobotController (Context)
        |
        +--> RobotState
        |      |
        |      +-- DisconnectedState
        |      +-- ConnectedState
        |      +-- PreparingState
        |      +-- ReadyState
        |      +-- DrivingState
        |      +-- JointBusyState
        |
        v
RobotClient
        |
        v
TCP/JSON -> C++ ROS2 Bridge -> ROS2 Driver -> RBY1 SDK -> gRPC -> Simulator
```

`MainWindow` không còn tự quyết định logic robot. Nó chỉ gửi yêu cầu cho
`RobotController`.

`RobotController` là **Context** của State Pattern.

Mỗi trạng thái quyết định hành vi hợp lệ.

## Chuyển trạng thái chính

```text
Disconnected
    |
    | TCP connected
    v
Connected
    |
    | Prepare
    v
Preparing
    |
    | success
    v
Ready
   / \
  /   \
Drive  Joint command
 |       |
 v       v
Driving JointBusy
 |       |
Stop   Action result
 |       |
 +---+---+
     |
     v
   Ready
```

Ngắt TCP ở bất kỳ trạng thái nào:

```text
Any State -> Disconnected
```

Cancel:

```text
Any connected state -> Connected
```

## Ý nghĩa từng State

### DisconnectedState

Chưa kết nối bridge.

Không cho phép điều khiển robot.

### ConnectedState

TCP đã kết nối nhưng robot chưa được xác nhận Ready.

Cho phép Prepare.

### PreparingState

Đang chờ phản hồi `prepare`.

Khóa Drive và Joint Control.

### ReadyState

Robot sẵn sàng.

Cho phép:

- điều khiển đế
- điều khiển 22 khớp
- Ready pose
- Zero pose
- Arms ready

### DrivingState

Đang gửi `/cmd_vel` mỗi 100 ms.

Thả nút -> Stop -> Ready.

### JointBusyState

Đang thực hiện ROS2 joint action.

Không cho gửi lệnh joint mới chồng lên.

Khi action trả về -> Ready.

## Backend command giữ nguyên

Qt vẫn dùng các JSON command:

```text
ping
status
prepare
power
servo
stream
cancel
velocity
stop
joints_status
joint_nudge
arms_ready
ready_pose
zero_pose
```

Không cần đổi TCP protocol nếu bridge Mốc 4 hiện tại đã hỗ trợ các command trên.

## Build

Dùng đúng Qt MinGW toolchain đã chạy được các mốc trước.

```powershell
$qtCmake = Get-ChildItem C:\Qt -Filter qt-cmake.bat -File -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match "\\mingw_64\\bin\\qt-cmake\.bat$" } | Select-Object -First 1

$qtGpp = Get-ChildItem C:\Qt\Tools -Filter g++.exe -File -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match "\\mingw.*_64\\bin\\g\+\+\.exe$" } | Select-Object -First 1

$mingwBin = Split-Path $qtGpp.FullName
$qtBin = Split-Path $qtCmake.FullName
```

Configure:

```powershell
& $qtCmake.FullName -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$mingwBin\gcc.exe" -DCMAKE_CXX_COMPILER="$mingwBin\g++.exe"
```

Build:

```powershell
cmake --build build-release --parallel
```

Deploy Qt runtime:

```powershell
& "$qtBin\windeployqt.exe" --release --compiler-runtime ".\build-release\RBY1DesktopQt.exe"
```

Chạy:

```powershell
& ".\build-release\RBY1DesktopQt.exe"
```

## Test State Pattern

Sau khi mở app:

```text
State: Disconnected
```

Kết nối:

```text
Disconnected -> Connected
```

Prepare:

```text
Connected -> Preparing -> Ready
```

Giữ nút Tiến:

```text
Ready -> Driving
```

Thả:

```text
Driving -> Ready
```

Nhấn joint +/-:

```text
Ready -> JointBusy -> Ready
```

Log trên giao diện cũng ghi:

```text
STATE: Connected -> Preparing
STATE: Preparing -> Ready
STATE: Ready -> Driving
STATE: Driving -> Ready
```
