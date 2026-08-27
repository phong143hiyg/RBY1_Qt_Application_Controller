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

Sau khi kết nối, ứng dụng luôn giữ state `Connected`, kể cả khi status của
bridge đang báo `ready=true`. Người dùng có thể nhấn `CHUẨN BỊ ROBOT` để bật
nhanh Power, Servo và Stream rồi chuyển sang `Preparing`, hoặc tự bật từng
công tắc; khi bridge xác nhận cả ba đã bật, ứng dụng tự chuyển sang
`Preparing` rồi `Ready`.

Ô `Ready` trên giao diện phản ánh state điều khiển của ứng dụng, không hiển thị
trực tiếp cờ `ready` cũ của bridge. Vì vậy ngay sau khi kết nối, ô này luôn là
`Không` cho đến khi thao tác chuẩn bị trong phiên kết nối hiện tại thành công.

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
set_ready_pose
clear_ready_pose
```

Bridge cần hỗ trợ `set_ready_pose` để lưu toàn bộ vị trí khớp hiện tại và
`clear_ready_pose` để xóa pose đã lưu. Command `ready_pose` phải đưa robot về
Ready pose động đã được lưu bởi `set_ready_pose`.

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
CHUẨN BỊ ROBOT -> Power ON + Servo ON + Stream ON
Connected -> Preparing -> Ready
```

Hoặc bật thủ công:

```text
Power ON -> Servo ON -> Stream ON -> Preparing -> Ready
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
