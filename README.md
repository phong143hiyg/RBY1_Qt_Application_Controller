# RBY1 Desktop Qt — Mốc 4 dùng State Pattern

Bản này refactor Mốc 4 sang **State Pattern**.

## Build nhanh

Mở PowerShell tại thư mục gốc của dự án rồi chạy:

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build-sdk --parallel
& 'C:\Qt\6.11.1\mingw_64\bin\windeployqt.exe' `
  --release --compiler-runtime --no-translations `
  '.\build-sdk\RBY1DesktopQt.exe'
```

File sau khi build:

```text
build-sdk\RBY1DesktopQt.exe
```

## Chạy simulator RBY1-M trên Windows

Simulator phải publish endpoint gRPC SDK trực tiếp trên port `50051`; ứng dụng không
dùng ROS 2, App Bridge hay TCP relay trung gian. Chạy lệnh sau trước khi mở ứng dụng:

```powershell
$env:RBY1_SIM_COMPOSE_DIR = "/path/in/wsl/to/rby1-docker"
powershell -ExecutionPolicy Bypass -File .\tools\start-rby1-sim.ps1
```

Khi thấy `RBY1-M simulator SDK is ready at 127.0.0.1:50051`, mở ứng dụng và giữ địa chỉ
robot mặc định `127.0.0.1:50051`.

## Nhập góc khớp

- Các ô màu xanh hiển thị góc thực tế theo độ (°), đọc trực tiếp qua RBY1 SDK mỗi 500 ms.
- Bấm vào ô để nhập góc đích tuyệt đối, rồi nhấn Enter để gửi lệnh; xử lý chuyển động giống kéo và nhả slider.
- Ứng dụng không đặt `minimum_time`: SDK tự chọn thời lượng ngắn nhất theo giới hạn đọc từ dynamics model. Lệnh dùng 40% vận tốc và 20% gia tốc cực đại để không vượt ngưỡng tracking-error của Control Manager.
- Trong lúc ô có focus, giá trị đang nhập không bị timer ghi đè. Bấm ra ngoài mà chưa Enter sẽ bỏ bản nháp và hiển thị lại góc robot báo gần nhất, sau đó tiếp tục cập nhật.
- Enter không tự đặt góc hiển thị thành góc đích: chỉ snapshot từ robot cập nhật giá trị xác nhận.
- Giá trị không hợp lệ, ngoài giới hạn thủ công, lệnh bị từ chối, timeout hoặc mất kết nối trong lúc chuyển động sẽ có popup thông báo. Không tự gửi lại lệnh thất bại.

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
IRby1Client (giao diện nội bộ)
        |
        v
SdkRobotClient -> Rainbow Robotics rby1-sdk -> gRPC -> RBY1/Simulator

PlanningPanel -> PlanningClient -> TCP/NDJSON v1 -> external planning backend
```

`MainWindow` không còn tự quyết định logic robot. Nó chỉ gửi yêu cầu cho
`RobotController`.

`RobotController` là **Context** của State Pattern.

Mỗi trạng thái quyết định hành vi hợp lệ.

## Chuyển trạng thái chính

```text
Disconnected
    |
    | SDK connected
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

Mất kết nối SDK ở bất kỳ trạng thái nào:

```text
Any State -> Disconnected
```

Cancel:

```text
Any connected state -> Connected
```

## Ý nghĩa từng State

### DisconnectedState

Chưa kết nối robot qua SDK.

Không cho phép điều khiển robot.

### ConnectedState

SDK đã kết nối nhưng robot chưa được xác nhận Ready.

Sau khi kết nối, ứng dụng luôn giữ state `Connected`, kể cả khi status của
SDK đang báo robot sẵn sàng. Người dùng có thể nhấn `CHUẨN BỊ ROBOT` để bật
nhanh Power, Servo và Control Manager rồi chuyển sang `Preparing`, hoặc tự bật từng
công tắc; khi SDK xác nhận cả ba đã bật, ứng dụng tự chuyển sang
`Preparing` rồi `Ready`.

Ô `Ready` trên giao diện phản ánh state điều khiển của ứng dụng, không hiển thị
trực tiếp trạng thái từ SDK. Vì vậy ngay sau khi kết nối, ô này luôn là
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

Đang gửi lệnh vận tốc qua SDK mỗi 100 ms.

Thả nút -> Stop -> Ready.

### JointBusyState

Đang thực hiện lệnh khớp qua SDK.

Không cho gửi lệnh joint mới chồng lên.

Khi action trả về -> Ready.

Thanh trượt và ô nhập góc gửi trực tiếp một mục tiêu tuyệt đối trong một motion
duy nhất. Ứng dụng không đặt thời gian tối thiểu; giới hạn vận tốc và gia tốc được
đọc từ dynamics model của endpoint rồi nhân lần lượt với 40% và 20%. Mức này tránh
MajorFault do tracking error trên simulator. Không có lệnh dịch chuyển tương đối hoặc vòng lặp
cộng sai số. `JointBusy` kết thúc khi SDK hoàn thành, báo lỗi hoặc hết thời gian chờ.
Khoảng điều khiển thủ công vẫn chừa 1° ở hai biên cơ khí. Riêng torso φ6 dùng
biên MuJoCo ±90° (UI ±89°), thay vì ±135° ghi trong URDF nhưng simulator không đạt được.

## Điều khiển robot qua SDK

Ứng dụng dùng C++ SDK chính thức của Rainbow Robotics, hỗ trợ model A và M. `SdkRobotClient`
chuyển API của SDK thành kết quả nội bộ cho `RobotController`; không gửi JSON
command tới App Bridge. Bản simulator trên máy này dùng địa chỉ mặc định
`127.0.0.1:50051`, có thể đặt lại bằng biến môi trường `RBY1_ROBOT_ADDRESS`.
SDK nằm trong `rby1-sdk-main`.
Chọn `RBY1-M` hoặc `RBY1-A` trên giao diện trước khi kết nối; biến môi trường
`RBY1_ROBOT_MODEL=a|m` đặt lựa chọn mặc định. Adapter kiểm tra số bậc tự do và
các nhóm khớp do endpoint báo về, nên model cấu hình sai sẽ bị từ chối.
Giới hạn vận tốc base mặc định là `0.30 m/s` và `0.60 rad/s`; có thể hạ bằng
`RBY1_MAX_LINEAR_VELOCITY` và `RBY1_MAX_ANGULAR_VELOCITY`.

`set_ready_pose` lưu vị trí khớp đo được trong phiên SDK hiện tại;
`clear_ready_pose` xóa bản lưu và `ready_pose` dùng bản lưu này.
`arms_ready` không cần pose đã lưu; nó co hai tay về preset Ready chuẩn của RBY1-M.
Planning v1 chỉ plan/preview, `execution_enabled=false`, và không sở hữu quyền
điều khiển robot.

## Test planning/preview độc lập

Tab **Test quỹ đạo** dùng `PlanningClient`/QTcpSocket riêng, default port **8082**,
không phụ thuộc Power/Servo/Control Manager của tab robot và không gửi command tới robot.
Đường robot command TCP/App Bridge 8081 đã được gỡ khỏi bản build chính. Tab
planning vẫn dùng socket NDJSON riêng; mốc này không có Execute.
Protocol mới: [planning NDJSON v1](planning_protocol/protocol-v1.md),
[fixture JSON dùng chung Qt/mock/backend](planning_protocol/fixtures/contract-v1.json).

Chạy mock độc lập bằng Python standard library (không cần ROS 2):

```powershell
python tools/mock_planning_server.py --host 127.0.0.1 --port 8082 --coalesce --fragment
```

Mở tab, nhập host/port, bấm **Kết nối planning**. Capability và scene được đọc từ
backend; chọn scenario rồi **Nạp scene**. Bảng hiển thị geometry/pose vật cản/vật gắp.
Pose dùng mét, quaternion **[x,y,z,w]**, norm=1; luôn có frame_id. Pick là TCP pose,
place là object pose; backend suy ra place TCP theo transform grasp/TCP offset.
Hướng approach/lift/lower/retreat và defaults lấy từ scene snapshot. Tọa độ mock
minh họa chưa chứng minh reachable/an toàn. Nhập mục tiêu và tham số rồi **Plan pose**
(dùng pick TCP làm goal) hoặc **Plan gắp–thả**. Xem stage/progress, lỗi và JSON metadata
plan_id/revision/duration_s/waypoint_count/validation. **Xem trước kế hoạch** gửi preview
theo plan_id; mock chỉ trả metadata. **Hủy planning** chờ terminal
task gốc; ACK cancel, timeout hoặc disconnect không xác nhận hủy. Kết nối lại hoặc
**Đối chiếu status** để kiểm tra task trước thao tác mới; không tự resubmit planning.
Scene/model đổi hoặc TTL hết làm plan mất hiệu lực.

MOCK chỉ test protocol/UI, không tính IK/collision/trajectory thật. Validation
mock luôn `simulated=true`, joint_limits/collision/timing=`not_checked`. Các chế độ:

```powershell
python tools/mock_planning_server.py --mode terminal-first --ack-delay 0.2
python tools/mock_planning_server.py --mode failure
python tools/mock_planning_server.py --mode timeout
python tools/mock_planning_server.py --delay 10
python tools/mock_planning_server.py --mode disconnect
python tools/mock_planning_server.py --mode stale-revision
```

Chạy từng server; dừng bằng Ctrl+C. `normal` mặc định ACK trước progress/terminal;
`terminal-first` dành cho test ACK đến muộn. `--fragment/--coalesce` kết hợp mọi mode.
Mode disconnect giữ worker/status trong cùng process để Qt reconnect. Delay lớn hơn
planning_timeout_s trả terminal TIMEOUT. Chạy delay để thử Hủy; server xác nhận khi
worker dừng. Các scenario âm chỉ mô phỏng lỗi, không là bằng chứng collision/NO_IK.

Backend planning tùy chọn phải triển khai protocol v1, giữ
`execution_enabled=false` và không có quyền gửi lệnh tới robot. Repository này
không cung cấp backend production; mock chỉ dùng để kiểm tra protocol/UI.

Sau configure/build Qt dưới đây, chạy toàn bộ test:

```powershell
$env:PATH = "$qtBin;$mingwBin;" + $env:PATH
ctest --test-dir build-sdk --output-on-failure
python tests/test_mock_planning.py
```

`RBY1SdkTests` kiểm tra adapter SDK không cần robot; `RBY1ControllerTests` dùng fake
`IRby1Client` để kiểm tra Unknown/Off/On, stale state, reconnect, giới hạn vận tốc,
dead-man stop, validation joint và khóa command đồng thời. `RBY1PlanningTests` kiểm tra correlation,
sequence, terminal-before-ACK, timeout/late response, cancel race, reconnect,
scene/model/validation, NDJSON 1 MiB và input/UI. Test tích hợp Qt tự khởi động mock
Python ở port động cho các mode. `PlanningMockTests` kiểm tra TCP mock/fixture,
BUSY/cancel/status/reconnect, preview không đổi scene, TTL và input. Python phải có
trong PATH; CMake đăng ký Python suite khi tìm thấy interpreter.

Để kiểm tra kết nối và đọc khớp từ robot/simulator mà không gửi lệnh chuyển động,
đặt `RBY1_TEST_ROBOT_ADDRESS=IP:port` rồi chạy `ctest --test-dir build-sdk -R RBY1SdkTests --output-on-failure`.
Test có motion bị khóa mặc định; chỉ đặt `RBY1_TEST_ENABLE_MOTION=1` khi endpoint là
simulator cô lập.

## Build Qt với SDK trong dự án

Dự án build trực tiếp mã nguồn `rby1-sdk` phiên bản `0.10.0` trong
`rby1-sdk-main`. Mã nguồn này được giải nén từ ZIP do người dùng cung cấp,
được pin tại tag `v0.10.0`, commit upstream
`9af8a734b7bef0167545e3d9f0d559276a5b64ee`. File
`rby1-sdk-main/UPSTREAM_REVISION` được CMake kiểm tra khi configure. ZIP GitHub
không chứa mã của các Git submodule; thư mục SDK trong dự án đã bổ sung đúng commit:

- `DynamixelSDK`: `886225ccaa9087c607a165b78c485a11ee0300f2`
- `osqp`: `236713ce9a56c182ac3230d52108f952afce1523`
- `osqp-eigen`: `743b419fd4390ebc950bb167bba13155a06b6de2`
- `qdldl` (OSQP yêu cầu khi cấu hình): tag `v0.1.8`, commit `138fdac58b9cd1c4137ff1b99152c8108a6cff5b`

`src/sdk/SdkRobotClient` là lớp nối ứng dụng với SDK, vẫn được giữ để các lớp
controller và UI không phải gọi API của nhà sản xuất trực tiếp. CMake không tìm
bản `rby1-sdk` đã cài ở máy nữa.

`thread.h` trong SDK có một nhánh tương thích MinGW để đặt tên thread bằng
`SetThreadDescription`; nhánh MSVC của upstream vẫn dùng SEH như bản gốc.

SDK vẫn cần các thư viện C++ từ Conan (gRPC, Eigen, tinyxml2, nlohmann_json).
Dùng cùng MinGW 13 và kiểu build Release với Qt để tránh lỗi ABI. Ví dụ trên
Windows với Qt 6 MinGW và Conan 2:

```powershell
conan profile detect
conan install rby1-sdk-main --output-folder=build-sdk/conan --profile:host=tools/sdk-mingw.profile --profile:build=default --build=missing
$toolchain = (Resolve-Path build-sdk/conan/conan_toolchain.cmake).Path
cmake -S . -B build-sdk -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64
cmake --build build-sdk --parallel
$env:PATH = 'C:/Qt/6.11.1/mingw_64/bin;C:/Qt/Tools/mingw1310_64/bin;' + $env:PATH
ctest --test-dir build-sdk --output-on-failure
& 'C:/Qt/6.11.1/mingw_64/bin/windeployqt.exe' --release --compiler-runtime --no-translations '.\build-sdk\RBY1DesktopQt.exe'
.\build-sdk\RBY1DesktopQt.exe
```

Trên máy dùng Qt ở đường dẫn khác, đổi `CMAKE_PREFIX_PATH` tương ứng. SDK được
liên kết tĩnh theo mặc định nên không cần sao chép DLL `rby1-sdk` riêng.
`windeployqt` phải chạy sau build để đặt DLL và plugin Qt đúng phiên bản cạnh
file `.exe`; nếu thiếu, Windows có thể nạp nhầm Qt/MinGW từ phần mềm khác trong `PATH`.

Với Qt MinGW 13.1, gRPC `1.72.0` có thể gặp lỗi GCC tại
`src/core/util/per_cpu.h:97`. Sau khi Conan tải source gRPC, script
`tools/patch-grpc-mingw13.ps1` có thể vá lỗi tương thích đó trong cache Conan
trước khi chạy lại `conan install`.

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
CHUẨN BỊ ROBOT -> Power ON + Servo ON + Control Manager ON
Connected -> Preparing -> Ready
```

Hoặc bật thủ công:

```text
Power ON -> Servo ON -> Control Manager ON -> Preparing -> Ready
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
