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

Docker Desktop trên máy này dành riêng port `50051`, vì vậy dự án dùng proxy cục bộ
`127.0.0.1:55051` để nối tới port `50051` của simulator. Chạy lệnh sau trước khi mở ứng dụng:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\start-rby1-sim.ps1
```

Khi thấy `RBY1-M simulator is ready at 127.0.0.1:55051`, mở ứng dụng và giữ địa chỉ
robot mặc định `127.0.0.1:55051`.

## Nhập góc khớp

- Các ô màu xanh hiển thị góc thực tế theo độ (°), đọc trực tiếp qua RBY1 SDK mỗi 500 ms.
- Bấm vào ô để nhập góc đích tuyệt đối, rồi nhấn Enter để gửi lệnh. Thời gian chuyển động dùng giá trị ở ô **Thời gian**; xử lý chuyển động giống kéo và nhả slider.
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
RobotClient (giao diện nội bộ)
        |
        v
SdkRobotClient -> Rainbow Robotics rby1-sdk -> gRPC -> RBY1/Simulator

PlanningPanel -> PlanningClient -> TCP/NDJSON v1 -> ROS 2 planning service
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

Với thanh trượt, nhập góc và nút ±1°, ứng dụng giữ góc đích tuyệt đối và
đọc trạng thái khớp mới trước khi gửi lệnh. Sau mỗi kết quả lệnh khớp,
ứng dụng đọc lại vị trí thực để tính delta tiếp theo; không cộng dồn phần
delta còn lại từ giá trị hiển thị cũ. Mỗi đoạn tối đa 0.20 rad, với thời
gian tối thiểu 0.20 s. `JointBusy` chỉ kết thúc khi vị trí đo được cách đích
không quá 0.05°, hoặc khi lỗi/timeout xảy ra; số đoạn hiệu chỉnh có giới hạn.
Nút ±1° dùng cùng khoảng điều khiển đã chừa 1° ở hai biên như thanh trượt.

## Điều khiển robot qua SDK

Ứng dụng dùng C++ SDK chính thức của Rainbow Robotics, hỗ trợ model A và M. `SdkRobotClient`
chuyển API của SDK thành kết quả nội bộ cho `RobotController`; không gửi JSON
command tới App Bridge. Bản simulator trên máy này dùng địa chỉ mặc định
`127.0.0.1:55051`, có thể đặt lại bằng biến môi trường `RBY1_ROBOT_ADDRESS`.
SDK nằm trong `rby1-sdk-main`.
Bản ứng dụng này được cấu hình cố định cho `RBY1-M` và tạo trực tiếp SDK model M.

`set_ready_pose` lưu vị trí khớp đo được trong phiên SDK hiện tại;
`clear_ready_pose` xóa bản lưu và `ready_pose` dùng bản lưu này.
`arms_ready` không cần pose đã lưu; nó co hai tay về preset Ready chuẩn của RBY1-M.
Chế độ điều khiển thủ công qua SDK và ROS 2 thực thi quỹ đạo không được cùng
sở hữu quyền điều khiển một robot. Planning v1 hiện chỉ plan/preview trên fake
hardware, `execution_enabled=false`.

## Test quỹ đạo MoveIt 2 (planning/preview)

Tab **Test quỹ đạo** dùng `PlanningClient`/QTcpSocket riêng, default port **8082**,
không phụ thuộc Power/Servo/Control Manager của tab robot và không gửi command tới robot.
Đường robot command TCP/App Bridge 8081 đã được gỡ khỏi bản build chính. Tab
planning vẫn dùng socket NDJSON riêng; mốc này không có Execute.
Protocol mới: [planning NDJSON v1](planning_protocol/protocol-v1.md),
[fixture JSON dùng chung Qt/mock/ROS](planning_protocol/fixtures/contract-v1.json).

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
plan_id/revision/duration_s/waypoint_count/validation. **Xem trước RViz** gửi preview
theo plan_id; mock chỉ trả metadata, không mở RViz. **Hủy planning** chờ terminal
task gốc; ACK cancel, timeout hoặc disconnect không xác nhận hủy. Kết nối lại hoặc
**Đối chiếu status** để kiểm tra task trước thao tác mới; không tự resubmit planning.
Scene/model đổi hoặc TTL hết làm plan mất hiệu lực.

MOCK chỉ test protocol/UI, không tính IK/collision/trajectory MoveIt. Validation
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

Kết nối ROS 2: dùng service planning **mới** trên Ubuntu/WSL triển khai đúng protocol
v1 và fixture chung, fake hardware, `execution_enabled=false`, `backend_mode=fake_hardware`.
Không dùng socket robot command 8081 hoặc giả định planning service có quyền điều khiển robot. Service ROS
mặc định bind localhost; để truy cập từ Windows khác máy/VM, cấu hình bind IP mạng
của service và port 8082, cho phép TCP đó trong firewall, nhập IP Ubuntu ở tab.
Với localhost forwarding WSL có thể dùng 127.0.0.1; nếu không forward được, dùng IP
WSL có thể truy cập. ROS sở hữu model/version/hash URDF/SRDF/config, frame, TCP,
scene/start state, IK/collision, MTC, TTL plan/status và RViz. Chỉ bật tính năng có
capability; model một tay phải xác minh trước. Repository này không cung cấp hoặc
tuyên bố đã chạy launch/planning ROS 2. Quy trình ROS ở
[đặc tả chung](question/prompts-test-moveit2-rby1.md); không có launch command chưa xác minh.

Sau configure/build Qt dưới đây, chạy toàn bộ test:

```powershell
$env:PATH = "$qtBin;$mingwBin;" + $env:PATH
ctest --test-dir build-sdk --output-on-failure
python tests/test_mock_planning.py
```

`RBY1SdkTests` kiểm tra adapter SDK không cần robot; `RBY1PlanningTests` kiểm tra correlation,
sequence, terminal-before-ACK, timeout/late response, cancel race, reconnect,
scene/model/validation, NDJSON 1 MiB và input/UI. Test tích hợp Qt tự khởi động mock
Python ở port động cho các mode. `PlanningMockTests` kiểm tra TCP mock/fixture,
BUSY/cancel/status/reconnect, preview không đổi scene, TTL và input. Python phải có
trong PATH; CMake đăng ký Python suite khi tìm thấy interpreter.

Để kiểm tra kết nối và đọc khớp từ robot/simulator mà không gửi lệnh chuyển động,
đặt `RBY1_TEST_ROBOT_ADDRESS=IP:port` rồi chạy `ctest --test-dir build-sdk -R RBY1SdkTests --output-on-failure`.

Kết quả triển khai, file thay đổi và các kiểm tra ROS 2 còn thiếu:
[báo cáo kiểm chứng Qt/mock](planning_protocol/verification.md).

## Build Qt với SDK trong dự án

Dự án build trực tiếp mã nguồn `rby1-sdk` phiên bản `0.10.0` trong
`rby1-sdk-main`. Mã nguồn này được giải nén từ ZIP do người dùng cung cấp,
khớp commit upstream `ac7e83056e0775c4680a50d8c7cb3d9c3c66164d`. ZIP GitHub
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
