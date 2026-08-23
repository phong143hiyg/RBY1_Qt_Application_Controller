# Các thuật toán và kiến trúc được sử dụng ngoài State Pattern

## 1. Phạm vi phân tích

Tài liệu này phân tích mã nguồn của ứng dụng desktop Qt trong project hiện tại:

```text
src/ui
src/controller
src/network
src/state
```

State Pattern đã được trình bày riêng nên tài liệu này không phân tích lại pattern đó.

Điểm cần phân biệt:

- **Kiến trúc** mô tả cách chia hệ thống thành các tầng/thành phần và cách chúng giao tiếp.
- **Design pattern** là một mẫu tổ chức class/object cho một vấn đề thường gặp.
- **Thuật toán** là chuỗi bước cụ thể để xử lý dữ liệu hoặc điều khiển luồng hoạt động.
- Các kỹ thuật như `RAII`, `QObject` parent–child và `std::unique_ptr` hỗ trợ thiết kế, nhưng không phải thuật toán.

## 2. Tổng quan kiến trúc toàn hệ thống

```text
Người dùng
    |
    | click / press / release
    v
MainWindow                    Tầng giao diện
    |
    | gọi API điều khiển
    v
RobotController               Tầng điều phối nghiệp vụ
    |
    | tạo command JSON
    v
RobotClient                   Tầng giao tiếp mạng
    |
    | TCP + JSON Lines
    v
C++ ROS2 Bridge               Tiến trình server bên ngoài project
    |
    v
ROS2 Driver -> RBY1 SDK -> gRPC -> Simulator/Robot

Chiều phản hồi:

Bridge -> RobotClient -> RobotController -> signal -> MainWindow
```

Phần desktop không điều khiển motor trực tiếp. Nó gửi yêu cầu cấp cao như `velocity`, `prepare`, `joint_nudge` hoặc `ready_pose` tới bridge.

## 3. Các kiến trúc và mẫu thiết kế ngoài State Pattern

## 3.1. Kiến trúc phân lớp (Layered Architecture)

Project chia trách nhiệm thành ba lớp chính:

| Lớp | Class chính | Trách nhiệm |
|---|---|---|
| Presentation | `MainWindow`, `ToggleSwitch` | Hiển thị, nhận thao tác người dùng |
| Application/Controller | `RobotController` | Điều phối use case, kiểm tra điều kiện, quản lý timer |
| Network/Infrastructure | `RobotClient` | Kết nối TCP, đóng gói/gỡ gói JSON |

Luồng phụ thuộc chính:

```text
MainWindow -> RobotController -> RobotClient -> QTcpSocket
```

Ví dụ, `MainWindow` không tự tạo JSON vận tốc:

```cpp
controller_->startDrive(0.12, 0.0, 0.0);
```

`RobotController` mới chuyển dữ liệu đó thành command:

```cpp
QJsonObject{
    {QStringLiteral("command"), QStringLiteral("velocity")},
    {QStringLiteral("linear_x"), linearX_},
    {QStringLiteral("linear_y"), linearY_},
    {QStringLiteral("angular_z"), angularZ_}
}
```

Lợi ích:

- UI không phụ thuộc chi tiết protocol TCP/JSON.
- `RobotClient` không cần biết nút nào trên UI vừa được nhấn.
- Logic điều khiển tập trung trong `RobotController`.
- Mỗi lớp có lý do thay đổi riêng.

### Có phải MVC hoàn chỉnh không?

Kiến trúc này **gần với MVC/MVP**, vì:

- `MainWindow` đóng vai trò View.
- `RobotController` đóng vai trò Controller/Presenter.
- Dữ liệu trạng thái robot đến từ bridge và được biểu diễn bằng JSON.

Tuy nhiên project không có một class Model miền riêng, ví dụ `RobotModel`. Vì vậy cách gọi chính xác hơn là **Layered Architecture hoặc MVC-like**, không nên khẳng định đây là MVC thuần túy.

## 3.2. Kiến trúc hướng sự kiện (Event-Driven Architecture)

Ứng dụng Qt không chạy một vòng lặp tự viết để liên tục kiểm tra nút bấm và socket. `QApplication::exec()` chạy event loop:

```cpp
return application.exec();
```

Khi một sự kiện xảy ra, Qt gọi slot hoặc lambda đã đăng ký:

```cpp
connect(
    forwardButton_,
    &QPushButton::pressed,
    this,
    [this]()
    {
        controller_->startDrive(0.12, 0.0, 0.0);
    });
```

Các nguồn sự kiện trong project gồm:

- Người dùng click, press hoặc release nút.
- `QTcpSocket` kết nối/ngắt kết nối.
- Socket có dữ liệu mới qua `readyRead`.
- Socket phát sinh lỗi.
- `QTimer` hết chu kỳ.
- `RobotController` phát state, log hoặc dữ liệu joint mới.

Kiến trúc này giúp UI không bị block trong lúc chờ mạng. Luồng giao tiếp có dạng:

```text
Event -> Signal -> Slot/Lambda -> Xử lý -> Phát signal mới
```

## 3.3. Observer Pattern qua Qt Signals/Slots

Signals/slots là cách Qt triển khai cơ chế quan sát–thông báo tương tự Observer Pattern.

Ví dụ `MainWindow` quan sát thay đổi từ controller:

```cpp
connect(
    controller_,
    &RobotController::stateChanged,
    this,
    &MainWindow::applyControllerState);
```

`RobotController` chỉ phát thông báo:

```cpp
emit stateChanged(
    state_->name(),
    state_->isConnected(),
    state_->canDrive(),
    state_->canControlJoints(),
    state_->canChangeSystemConfiguration(),
    state_->isBusy());
```

Controller không cần gọi trực tiếp từng widget. View đăng ký nhận signal và tự cập nhật.

Các quan hệ quan sát khác:

```text
QTcpSocket.connected       -> RobotClient.bridgeConnected
QTcpSocket.readyRead       -> RobotClient xử lý buffer
RobotClient.responseReceived -> RobotController.handleResponse
RobotController.logMessage -> MainWindow.appendLog
RobotController.jointStatusReceived -> MainWindow.updateJointDisplay
QTimer.timeout             -> gửi velocity/status
```

Lợi ích là giảm liên kết trực tiếp giữa nơi phát sự kiện và nơi xử lý sự kiện.

## 3.4. Kiến trúc Client–Server qua TCP

`RobotClient` là TCP client; C++ ROS2 Bridge là TCP server.

```text
Desktop Qt TCP Client <---- TCP ----> C++ ROS2 Bridge TCP Server
```

Client kết nối mặc định tới:

```text
127.0.0.1:8081
```

Mỗi yêu cầu được serialize thành JSON compact và thêm ký tự xuống dòng:

```cpp
QByteArray payload =
    QJsonDocument(command).toJson(QJsonDocument::Compact);

payload.append('\n');
socket_.write(payload);
```

Ví dụ dữ liệu truyền trên TCP:

```json
{"command":"velocity","linear_x":0.12,"linear_y":0,"angular_z":0}
```

Ký tự `\n` ở cuối tạo biên giữa các JSON message. Cách này thường được gọi là **JSON Lines**, **newline-delimited JSON** hoặc **NDJSON framing**.

## 3.5. Kiến trúc Request–Response bất đồng bộ

Sau khi gửi command, chương trình không đứng chờ phản hồi. `sendCommand()` trả về ngay, còn response được nhận sau qua signal `readyRead`.

```text
Gửi request
    |
    +--> UI vẫn tiếp tục xử lý event
    |
Sau đó socket có dữ liệu
    |
    v
readyRead -> parse JSON -> responseReceived
```

Điều này phù hợp với GUI vì nếu chờ mạng đồng bộ trên UI thread, cửa sổ có thể bị treo.

Lưu ý: `disconnectFromBridge()` có gọi `waitForDisconnected(500)`, nên riêng lúc đóng kết nối có thể chờ tối đa 500 ms. Các request điều khiển thông thường vẫn theo cách bất đồng bộ.

## 3.6. Application Controller / Facade-like

`RobotController` cung cấp một API đơn giản cho View:

```cpp
prepareRobot();
startDrive(...);
stopDrive();
nudgeJoint(...);
sendPose(...);
```

View không cần biết:

- Cấu trúc JSON của từng command.
- Cách gửi qua `QTcpSocket`.
- Chu kỳ gửi vận tốc.
- Cách ghép request với response.
- Khi nào cần refresh joint.

Vì vậy `RobotController` có vai trò **Application Controller** và mang tính chất **Facade-like**: nó tạo một mặt tiền đơn giản che các chi tiết của tầng dưới. Không nhất thiết gọi đây là GoF Facade hoàn chỉnh, nhưng ý tưởng che giấu subsystem là rõ ràng.

## 3.7. Quản lý sở hữu theo RAII

Project dùng hai cơ chế quản lý vòng đời:

### QObject parent–child

```cpp
controller_(new RobotController(this))
client_(new RobotClient(this))
```

Khi object cha bị hủy, Qt tự hủy các `QObject` con. Vì vậy không cần `delete controller_` hoặc `delete client_` thủ công.

### `std::unique_ptr`

State hiện tại có đúng một chủ sở hữu:

```cpp
std::unique_ptr<RobotState> state_;
```

Đây là RAII và ownership management, không phải một thuật toán hay một kiến trúc riêng. Nó giúp code an toàn khi có return sớm hoặc exception.

## 4. Các thuật toán xử lý được sử dụng

## 4.1. Thuật toán tách message từ TCP stream

### Bài toán

TCP là luồng byte, không bảo đảm mỗi lần `readAll()` nhận đúng một JSON. Có thể xảy ra:

```text
Lần đọc 1: {"success":tr
Lần đọc 2: ue}\n{"ready":true}\n
```

Hoặc một lần đọc nhận nhiều message cùng lúc. Vì vậy không thể parse trực tiếp mỗi `readAll()` thành một JSON.

### Dữ liệu dùng

```cpp
QByteArray receiveBuffer_;
```

### Các bước

```text
1. Đọc toàn bộ byte hiện có từ socket.
2. Nối byte mới vào receiveBuffer_.
3. Tìm ký tự '\n'.
4. Nếu chưa có '\n', giữ buffer và chờ lần đọc tiếp theo.
5. Nếu có, lấy phần trước '\n' làm một frame.
6. Xóa frame vừa lấy khỏi buffer.
7. Bỏ qua dòng rỗng.
8. Parse frame thành JSON object.
9. Phát responseReceived.
10. Lặp lại để xử lý các frame còn lại.
```

Code chính:

```cpp
receiveBuffer_.append(socket_.readAll());

while (true)
{
    const qsizetype newlineIndex =
        receiveBuffer_.indexOf('\n');

    if (newlineIndex < 0)
    {
        return;
    }

    QByteArray line =
        receiveBuffer_.left(newlineIndex).trimmed();

    receiveBuffer_.remove(0, newlineIndex + 1);

    // Parse line...
}
```

Thuật toán xử lý đúng cả hai trường hợp message bị chia nhỏ và nhiều message bị gộp trong một lần đọc.

## 4.2. Thuật toán ghép request–response bằng hàng đợi FIFO

Khi gửi request, project lưu tên thao tác vào hàng đợi:

```cpp
QQueue<QString> pendingOperations_;
pendingOperations_.enqueue(operationName);
```

Khi nhận một response hoàn chỉnh, tên thao tác đầu hàng được lấy ra:

```cpp
const QString operationName =
    pendingOperations_.isEmpty()
        ? QStringLiteral("Phản hồi")
        : pendingOperations_.dequeue();
```

Luồng ví dụ:

```text
Gửi: ping       -> queue = [Ping]
Gửi: status     -> queue = [Ping, Đọc trạng thái]
Nhận response 1 -> lấy Ping
Nhận response 2 -> lấy Đọc trạng thái
```

Đây là ánh xạ **FIFO: First In, First Out**.

### Điều kiện để đúng

Bridge phải trả đúng một response cho mỗi request và giữ nguyên thứ tự. Nếu server trả response không theo thứ tự hoặc có notification tự phát, cách ghép bằng FIFO có thể gắn sai `operationName`.

Giải pháp mạnh hơn cho hệ thống phức tạp là thêm `request_id` vào cả request và response, rồi dùng `QHash<request_id, operation>` để ghép theo ID.

## 4.3. Thuật toán phát lệnh vận tốc định kỳ

Robot cần tiếp tục nhận velocity trong khi người dùng giữ nút. Project dùng `QTimer` chu kỳ 100 ms, tương đương khoảng 10 Hz:

```cpp
velocityTimer_.setInterval(100);
```

Khi nhấn nút:

```text
1. Lưu linearX, linearY, angularZ mới.
2. Gửi ngay một velocity command để giảm độ trễ cảm nhận.
3. Nếu timer chưa chạy thì khởi động timer.
4. Mỗi 100 ms, timer gửi lại velocity hiện tại.
```

```cpp
void RobotController::startVelocityInternal(
    double linearX,
    double linearY,
    double angularZ)
{
    linearX_ = linearX;
    linearY_ = linearY;
    angularZ_ = angularZ;

    sendVelocityTick();

    if (!velocityTimer_.isActive())
    {
        velocityTimer_.start();
    }
}
```

Khi thả nút:

```text
1. Dừng timer.
2. Đưa ba thành phần vận tốc về 0.
3. Nếu còn kết nối thì gửi command stop.
```

```cpp
void RobotController::stopVelocityInternal()
{
    velocityTimer_.stop();
    linearX_ = 0.0;
    linearY_ = 0.0;
    angularZ_ = 0.0;
    // Gửi stop nếu còn kết nối.
}
```

Sự kết hợp `pressed -> start` và `released -> stop` tạo hành vi **hold-to-run/dead-man control**: chỉ tiếp tục chạy khi người dùng còn giữ nút.

## 4.4. Thuật toán ánh xạ nút điều hướng sang vector vận tốc

Mỗi nút được ánh xạ thành bộ:

```text
(linear_x, linear_y, angular_z)
```

| Nút | Bộ vận tốc | Ý nghĩa |
|---|---|---|
| Tiến | `(0.12, 0.0, 0.0)` | Tịnh tiến về trước |
| Lùi | `(-0.12, 0.0, 0.0)` | Tịnh tiến về sau |
| Rẽ trái | `(0.10, 0.0, 0.30)` | Vừa tiến vừa quay trái |
| Rẽ phải | `(0.10, 0.0, -0.30)` | Vừa tiến vừa quay phải |
| Xoay trái | `(0.0, 0.0, 0.35)` | Quay tại chỗ sang trái |
| Xoay phải | `(0.0, 0.0, -0.35)` | Quay tại chỗ sang phải |

Đây là ánh xạ lệnh rời rạc của UI sang vector điều khiển vận tốc. Project không tính quỹ đạo tối ưu; các giá trị được cấu hình cố định trong code.

## 4.5. Thuật toán polling trạng thái có chống request chồng nhau

Status được hỏi định kỳ mỗi 500 ms, tương đương khoảng 2 Hz:

```cpp
statusTimer_.setInterval(500);
```

Biến cờ:

```cpp
bool statusRequestPending_{false};
```

ngăn gửi một request status mới khi request trước chưa có response:

```cpp
void RobotController::requestStatus()
{
    if (statusRequestPending_)
    {
        return;
    }

    statusRequestPending_ =
        sendSimpleInternal(
            QStringLiteral("status"),
            QStringLiteral("Đọc trạng thái"));
}
```

Khi nhận đúng response status, cờ được mở lại:

```cpp
if (operationName == QStringLiteral("Đọc trạng thái"))
{
    statusRequestPending_ = false;
}
```

Thuật toán này tạo quy tắc **at most one in-flight status request**:

```text
Không có status đang chờ -> được gửi request mới
Có status đang chờ       -> bỏ qua tick timer hiện tại
Nhận response            -> cho phép gửi lần tiếp theo
```

Nó tránh hàng đợi tăng liên tục khi bridge trả lời chậm hơn chu kỳ polling.

### Giới hạn

Nếu request đã gửi thành công nhưng response không bao giờ về trong khi TCP vẫn giữ kết nối, `statusRequestPending_` có thể giữ `true` mãi. Một phiên bản mạnh hơn có thể thêm response timeout để tự mở khóa hoặc reconnect.

## 4.6. Thuật toán đồng bộ trạng thái remote với trạng thái local

Phản hồi `status` có thể chứa trường `ready`. Controller dùng nó để đồng bộ trạng thái local:

```text
Nếu response không có ready -> không làm gì
Nếu success = false         -> không làm gì
Nếu ready = true            -> chuyển local về Ready nếu cần
Nếu ready = false           -> chuyển local về Connected nếu cần
```

Việc đồng bộ bị bỏ qua khi đang busy hoặc đang drive:

```cpp
if (
    operationName == QStringLiteral("Đọc trạng thái")
    && !state_->isBusy()
    && state_->name() != QStringLiteral("Driving"))
{
    updateStateFromStatus(response);
}
```

Mục đích là tránh một response polling đến không đúng thời điểm ghi đè tiến trình local đang hoạt động, ví dụ đang chờ joint action hoặc đang điều khiển đế.

Đây là một thuật toán **reconciliation**: so sánh trạng thái được báo từ hệ thống bên ngoài với trạng thái local rồi điều chỉnh local khi an toàn.

## 4.7. Thuật toán kiểm soát khả năng thao tác trên UI

Controller phát ra các capability:

```text
connected
canDrive
canControlJoints
canChangeSystemConfiguration
busy
```

View dùng biểu thức Boolean để bật/tắt nhóm widget:

```cpp
driveGroup_->setEnabled(
    connected && canDrive && !busy);

upperBodyContent_->setEnabled(
    connected && canControlJoints && !busy);
```

Luồng xử lý:

```text
Controller thay đổi trạng thái
        |
        v
Phát stateChanged kèm capability
        |
        v
MainWindow tính điều kiện Boolean
        |
        v
Bật/tắt widget phù hợp
```

Đây là presentation logic giúp ngăn người dùng gửi thao tác không hợp lệ ngay từ giao diện. Controller vẫn kiểm tra lại để bảo đảm an toàn; UI disable không thay thế validation ở tầng nghiệp vụ.

Khi code tự đổi trạng thái các switch, project tạm chặn signal:

```cpp
powerSwitch_->blockSignals(true);
powerSwitch_->setChecked(true);
powerSwitch_->blockSignals(false);
```

Mục đích là tránh cập nhật giao diện vô tình phát sinh một command mới quay trở lại controller.

## 4.8. Thuật toán parse và ánh xạ vị trí joint lên UI

Response joint có thể có một trong hai dạng:

```json
{
  "groups": {
    "torso": [0.1, 0.2]
  }
}
```

hoặc:

```json
{
  "torso": {
    "positions": [0.1, 0.2]
  }
}
```

`updateJointDisplay()` chuẩn hóa cả hai dạng bằng các bước:

```text
1. Nếu có object groups thì dùng groups; nếu không dùng response gốc.
2. Duyệt bốn group: torso, head, right_arm, left_arm.
3. Tìm dữ liệu của group.
4. Chấp nhận dữ liệu là array trực tiếp hoặc object có array positions.
5. Tìm danh sách QLabel tương ứng trong QHash.
6. Lấy min(số position, số label).
7. Cập nhật từng label và định dạng 3 chữ số thập phân.
```

Đoạn giới hạn số phần tử:

```cpp
const int count =
    qMin(
        static_cast<int>(positions.size()),
        static_cast<int>(labels.size()));
```

giúp tránh truy cập vượt giới hạn nếu bridge trả số joint khác số label trên giao diện.

Với `G` group và tổng `J` joint được cập nhật, độ phức tạp gần `O(G + J)`. Trong project, `G = 4` nên chi phí rất nhỏ.

## 4.9. Thuật toán tìm trường status theo nhiều khóa thay thế

Bridge có thể dùng các tên field khác nhau, ví dụ:

```text
power / power_on / powered
servo / servo_on / servo_enabled
stream / stream_on / streaming / stream_enabled
```

Hàm `findStatusValue()` duyệt danh sách khóa ưu tiên:

```text
Với mỗi key:
    Nếu key có ở response gốc -> trả value
    Nếu key có ở object status -> trả value
Không tìm thấy -> trả undefined
```

```cpp
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
```

Đây là thuật toán fallback theo thứ tự ưu tiên, giúp UI chịu được một số biến thể schema của bridge.

Nếu có `K` khóa thay thế, độ phức tạp là `O(K)`. `QJsonObject::contains()` và `value()` có chi phí tra cứu phụ thuộc implementation của Qt, nhưng `K` trong project rất nhỏ.

## 4.10. Thuật toán rút gọn log status

Thay vì in toàn bộ JSON status sau mỗi 500 ms, `compactStatusText()`:

```text
1. Chọn các trường quan trọng.
2. Tìm field bằng các alias được hỗ trợ.
3. Chuyển bool, number và string thành text.
4. Bỏ qua object/array và field không tồn tại.
5. Ghép kết quả bằng " | ".
```

Ví dụ:

```text
Trạng thái: success=true | state=ready | power=true | servo=true
```

Response `Velocity` còn được bỏ qua hoàn toàn ở log UI:

```cpp
if (operationName == QStringLiteral("Velocity"))
{
    return;
}
```

Nếu không lọc, lệnh 10 Hz sẽ làm log tăng rất nhanh và che mất các sự kiện quan trọng.

## 4.11. Thuật toán sinh giao diện joint theo dữ liệu cấu hình

`buildJointGroup()` nhận:

```text
title, groupName, jointCount
```

rồi dùng vòng lặp để tạo mỗi hàng gồm:

```text
Tên joint | Giá trị hiện tại | Nút - | Nút +
```

Mỗi lambda bắt `groupName` và `index` tương ứng:

```cpp
[this, groupName, index]()
{
    controller_->nudgeJoint(
        groupName,
        index,
        jointStepSpinBox_->value(),
        minimumTimeSpinBox_->value());
}
```

Các label giá trị được lưu theo group:

```cpp
QHash<QString, QVector<QLabel *>> jointValueLabels_;
```

Nhờ đó project không cần viết thủ công event handler riêng cho từng joint. Với `N` joint, thời gian và số widget được tạo đều là `O(N)`.

## 4.12. Thuật toán refresh joint có độ trễ

Sau khi joint action thành công, project không hỏi vị trí mới ngay lập tức mà đặt lịch sau 300 ms:

```cpp
QTimer::singleShot(
    300,
    this,
    &RobotController::refreshJoints);
```

Mục đích là cho bridge/ROS2 một khoảng thời gian ngắn để dữ liệu joint status được cập nhật trước khi đọc lại. Đây là **delayed refresh/deferred execution**.

Giới hạn: 300 ms là giá trị cố định, không phải điều kiện xác nhận dữ liệu đã sẵn sàng. Cơ chế dựa trên event hoặc version/timestamp từ backend sẽ chắc chắn hơn nếu hệ thống cần độ tin cậy cao.

## 4.13. Thuật toán xử lý lỗi và khôi phục kết nối

Khi socket ngắt:

```text
1. Xóa receive buffer.
2. Xóa hàng đợi operation đang chờ.
3. Phát bridgeDisconnected.
4. Dừng velocity timer.
5. Dừng status timer.
6. Xóa cờ status pending.
7. Đưa trạng thái điều khiển về Disconnected.
8. Cập nhật UI qua signal.
```

Việc xóa buffer và queue rất quan trọng: response của phiên kết nối cũ không được phép ghép với request của phiên mới.

Nếu `socket_.write()` lỗi ngay, code hiện tại cố gắng rollback metadata trong queue:

```cpp
if (socket_.write(payload) < 0)
{
    pendingOperations_.dequeue();
    emit clientError(socket_.errorString());
    return false;
}
```

Tuy nhiên, `dequeue()` xóa **phần tử đầu queue**, trong khi operation vừa thêm nằm ở cuối queue. Cách này chỉ đúng nếu trước đó queue rỗng. Nếu đã có request đang chờ, write mới thất bại sẽ làm mất metadata của request cũ và có thể ghép sai các response tiếp theo.

Ý định rollback đúng nên là xóa phần tử cuối vừa thêm, ví dụ `removeLast()`, hoặc chỉ enqueue sau khi `write()` thành công:

```cpp
if (socket_.write(payload) < 0)
{
    emit clientError(socket_.errorString());
    return false;
}

pendingOperations_.enqueue(operationName);
```

Đây là một điểm cần lưu ý trong implementation hiện tại, không phải đặc tính của thuật toán FIFO.

## 4.14. Trình tự đóng ứng dụng an toàn

Khi cửa sổ đóng:

```cpp
void MainWindow::closeEvent(QCloseEvent *event)
{
    controller_->stopDrive();
    controller_->disconnectFromBridge();
    QMainWindow::closeEvent(event);
}
```

Trình tự là:

```text
Stop robot -> ngắt TCP -> cho QMainWindow đóng
```

Mục tiêu là không để timer velocity tiếp tục hoạt động và cố gắng gửi lệnh sau khi giao diện đã bị hủy.

## 4.15. Thuật toán vẽ `ToggleSwitch` tùy biến

`ToggleSwitch` kế thừa `QCheckBox` và override `paintEvent()`.

Các bước vẽ:

```text
1. Tính kích thước track và thumb.
2. Chọn vị trí thumb theo isChecked().
3. Vẽ track bo tròn.
4. Chọn màu xanh khi bật, xám khi tắt.
5. Vẽ thumb màu trắng.
6. Vẽ text bên phải switch.
```

Vị trí ngang của thumb:

```cpp
int thumbX = isChecked()
    ? trackWidth - margin - 2 * thumbRadius
    : margin;
```

`sizeHint()` tính kích thước từ độ rộng track và font; `hitButton()` cho phép toàn bộ vùng widget nhận click.

Đây là custom rendering/presentation algorithm, không phải một design pattern cấp hệ thống.

## 5. Các cơ chế an toàn và kiểm tra dữ liệu

Project còn dùng nhiều guard clause:

```cpp
if (!client_->isConnected())
{
    return;
}
```

```cpp
if (!response.contains(QStringLiteral("ready")))
{
    return;
}
```

```cpp
if (positions.isEmpty())
{
    continue;
}
```

Các guard này tạo nguyên tắc **fail fast**: dữ liệu hoặc điều kiện không hợp lệ được loại bỏ sớm, giảm độ lồng nhau của code và tránh truy cập dữ liệu sai.

JSON cũng được kiểm tra trước khi sử dụng:

```cpp
if (
    parseError.error != QJsonParseError::NoError
    || !document.isObject())
{
    emit clientError(...);
    continue;
}
```

## 6. Những thứ không nên gọi nhầm là pattern hoặc thuật toán

### JSON command không phải GoF Command Pattern

Project có các message mang trường `"command"`, nhưng không có hierarchy `Command`, hàm `execute()` hoặc cơ chế undo. Vì vậy đây là **command message trong protocol**, không phải Command Design Pattern.

### Project không có thuật toán điều khiển robot cấp thấp

Trong source desktop hiện tại không có:

- PID control.
- Inverse kinematics.
- Forward kinematics.
- Path planning như A*, Dijkstra hoặc RRT.
- Trajectory generation nội suy joint.
- Collision avoidance.

Các công việc đó, nếu có, nằm ở ROS2 Driver, RBY1 SDK, bridge hoặc simulator bên ngoài phần source đang phân tích. Desktop chỉ phát lệnh và hiển thị phản hồi.

### Không phải multithreading tự quản lý

Project không tạo `std::thread` hoặc `QThread`. Tính bất đồng bộ hiện tại chủ yếu đến từ Qt event loop, socket signals và timers trên thread của ứng dụng.

## 7. Bảng tổng hợp để thuyết trình

| Nhóm | Tên | Nơi sử dụng | Mục đích |
|---|---|---|---|
| Kiến trúc | Layered/MVC-like | `MainWindow -> RobotController -> RobotClient` | Tách UI, nghiệp vụ và network |
| Kiến trúc | Event-driven | Qt event loop, signals/slots, timers | Xử lý UI và mạng không chặn |
| Pattern | Observer | Signals/slots | Thông báo thay đổi giữa các object |
| Kiến trúc | Client–Server | `RobotClient` và bridge | Giao tiếp desktop với ROS2 bridge |
| Protocol | JSON Lines qua TCP | `sendCommand()`, `processIncomingLines()` | Phân tách JSON trên TCP stream |
| Thiết kế | Application Controller/Facade-like | `RobotController` | Cung cấp API đơn giản cho View |
| Kỹ thuật | RAII/ownership | QObject parent, `unique_ptr` | Quản lý vòng đời object an toàn |
| Thuật toán | TCP stream framing | `receiveBuffer_` + tìm `\n` | Ghép/tách message hoàn chỉnh |
| Thuật toán | FIFO correlation | `pendingOperations_` | Ghép response với tên request |
| Thuật toán | Periodic velocity streaming | Timer 100 ms | Gửi vận tốc liên tục khi giữ nút |
| Thuật toán | Status polling có guard | Timer 500 ms + cờ pending | Tránh gửi status chồng nhau |
| Thuật toán | Remote/local reconciliation | `updateStateFromStatus()` | Đồng bộ trạng thái với bridge |
| Thuật toán | Capability-based UI gating | `applyControllerState()` | Bật/tắt thao tác hợp lệ |
| Thuật toán | Flexible JSON parsing | `updateJointDisplay()` | Hỗ trợ nhiều dạng response |
| Thuật toán | Alias lookup | `findStatusValue()` | Hỗ trợ nhiều tên field |
| Thuật toán | Dynamic UI generation | `buildJointGroup()` | Tránh lặp code cho từng joint |
| Thuật toán | Delayed refresh | `QTimer::singleShot(300, ...)` | Đọc joint sau khi action cập nhật |
| Thuật toán | Safe shutdown/recovery | disconnect và `closeEvent()` | Dừng điều khiển và dọn trạng thái |
| Thuật toán UI | Custom painting | `ToggleSwitch::paintEvent()` | Vẽ switch theo trạng thái |

## 8. Câu trả lời ngắn để trình bày với giảng viên

> Ngoài State Pattern, project sử dụng kiến trúc phân lớp theo hướng MVC-like: `MainWindow` là View, `RobotController` là tầng điều phối và `RobotClient` là tầng network. Ứng dụng hoạt động theo kiến trúc hướng sự kiện của Qt và dùng Observer Pattern qua signals/slots. Desktop giao tiếp với ROS2 bridge theo mô hình client–server, truyền các JSON message được phân tách bằng ký tự xuống dòng trên TCP. Các thuật toán chính gồm tích lũy buffer và tách JSON frame, ghép request–response bằng hàng đợi FIFO, gửi velocity định kỳ 100 ms khi giữ nút, polling status 500 ms với cờ chống request chồng, đồng bộ trạng thái bridge với trạng thái local, parse nhiều dạng JSON joint, bật/tắt UI theo capability và sinh động các control joint bằng vòng lặp. Project còn dùng RAII qua QObject parent–child và `unique_ptr` để quản lý vòng đời. Phần desktop không cài đặt PID, inverse kinematics hay path planning; các thuật toán robot cấp thấp thuộc bridge/ROS2/SDK bên ngoài project này.

Tóm tắt dễ nhớ:

```text
UI phát sự kiện
    -> Controller điều phối
    -> Client đóng gói JSON
    -> TCP gửi tới bridge
    -> response được tách frame và ghép FIFO
    -> signal đưa kết quả ngược về UI
```
