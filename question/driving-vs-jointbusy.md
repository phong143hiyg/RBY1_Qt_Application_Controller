# Phân biệt State `Driving` và `JointBusy`

## Câu hỏi

> State `Driving` và `JointBusy` nghe rất giống nhau. Phân biệt State `Driving` và `JointBusy`, giải thích chi tiết vì sao phải sinh ra hai state khác nhau này.

## Trả lời

Hai state này chỉ giống nhau ở bề ngoài: trong cả hai trường hợp, robot đều đang thực hiện chuyển động và đều không được phép nhận một số lệnh xung đột. Tuy nhiên, chúng đại diện cho hai loại hoạt động có vòng đời hoàn toàn khác nhau:

- `Driving` là trạng thái điều khiển vận tốc liên tục của đế robot.
- `JointBusy` là trạng thái chờ một lệnh điều khiển khớp hoặc pose hoàn tất.

Nói ngắn gọn:

> `Driving` là một chế độ điều khiển liên tục, kết thúc bởi người dùng.  
> `JointBusy` là một giao dịch bất đồng bộ, kết thúc bởi response từ bridge.

## 1. So sánh trực tiếp

| Tiêu chí | `Driving` | `JointBusy` |
|---|---|---|
| Đối tượng được điều khiển | Đế di chuyển của robot | Khớp hoặc pose của robot |
| Loại lệnh | Lệnh vận tốc liên tục | Lệnh tác vụ một lần |
| Ví dụ | `velocity`, `stop` | `joint_nudge`, `arms_ready`, `zero_pose`, `ready_pose` |
| Cách gửi lệnh | Gửi ngay và lặp lại mỗi 100 ms | Gửi một lần rồi chờ response |
| Ai quyết định kết thúc? | Người dùng thả nút hoặc nhấn Stop | Bridge trả kết quả đúng với thao tác đang chờ |
| Có được cập nhật lệnh khi đang chạy? | Có, được đổi hướng hoặc vận tốc | Không, lệnh joint/pose mới bị từ chối |
| Hàm kết thúc state | `stopDrive()` | `onResponse()` |
| Dữ liệu cần ghi nhớ | Vận tốc hiện tại nằm trong controller | Tên thao tác đang chờ trong `pendingOperation_` |
| `canDrive()` | `true` | `false` |
| `isBusy()` | `false` | `true` |
| State tiếp theo | `Ready` sau khi Stop | `Ready` sau response thành công hoặc thất bại |

## 2. Bản chất của `Driving`

Khi robot đang ở `Ready`, người dùng giữ nút điều hướng thì `ReadyState::startDrive()` thực hiện hai việc:

1. Gọi `startVelocityInternal()`.
2. Chuyển state từ `Ready` sang `Driving`.

Trong controller, `startVelocityInternal()`:

- Lưu các giá trị `linearX`, `linearY` và `angularZ`.
- Gửi một lệnh velocity ngay lập tức.
- Khởi động timer để gửi lại velocity sau mỗi 100 ms.

Do đó, `Driving` không biểu diễn việc chương trình đang chờ một lệnh hoàn thành. Nó biểu diễn robot đang ở trong chế độ nhận một dòng lệnh vận tốc liên tục.

Trong lúc ở `Driving`, người dùng vẫn có thể gọi `startDrive()` lần nữa để đổi hướng hoặc thay đổi tốc độ. State vẫn là `Driving`; chỉ các giá trị vận tốc được cập nhật:

```text
Driving --startDrive(vận tốc mới)--> Driving
```

`DrivingState::startDrive()` trả về `nullptr`, nghĩa là không yêu cầu chuyển sang state khác.

`Driving` chỉ kết thúc khi người dùng chủ động thả nút hoặc ra lệnh Stop:

```text
Driving --stopDrive()--> Ready
```

Khi đó, `stopDrive()` sẽ:

- Dừng timer gửi velocity.
- Đặt vận tốc về 0.
- Gửi lệnh `stop` tới bridge.
- Trả về một `ReadyState` mới.

Vì vậy, vòng đời của `Driving` là:

```text
Giữ nút      → Driving
Đổi hướng    → vẫn Driving
Thả nút/Stop → Ready
```

## 3. Bản chất của `JointBusy`

`JointBusy` xuất hiện khi người dùng gửi một thao tác như:

- Tăng hoặc giảm một khớp.
- Đưa tay về pose chuẩn.
- Đưa robot về zero pose.
- Lưu hoặc xóa ready pose.

Trước khi gửi lệnh joint/pose, chương trình gọi `stopVelocityInternal()`. Điều này bảo đảm robot không vừa chạy đế vừa thực hiện một chuyển động khớp có thể gây xung đột. Sau khi lệnh được gửi thành công, state chuyển từ `Ready` sang `JointBusy`.

Ví dụ với joint nudge:

```text
Ready
  → dừng velocity
  → gửi joint_nudge một lần
  → JointBusy("Joint nudge")
```

Khác với `Driving`, chương trình không gửi lại `joint_nudge` mỗi 100 ms. Lệnh chỉ được gửi một lần, sau đó ứng dụng phải chờ bridge thực thi và trả kết quả.

`JointBusyState` lưu tên thao tác đang chờ trong `pendingOperation_`. Khi nhận response, state kiểm tra:

```cpp
if (operationName != pendingOperation_)
{
    return nullptr;
}
```

Một response không liên quan, chẳng hạn response của `Joints status`, không được phép làm kết thúc thao tác hiện tại. Chỉ response có `operationName` khớp với `pendingOperation_` mới chuyển state về `Ready`.

Luồng của `JointBusy` là:

```text
Ready
  --gửi joint/pose-->
JointBusy
  --response đúng operation, success=true hoặc false-->
Ready
```

Cả kết quả thành công và thất bại đều quay về `Ready`, vì response đã xác nhận giao dịch hiện tại kết thúc. Nếu thành công, chương trình còn lên lịch đọc lại trạng thái khớp sau 300 ms để cập nhật dữ liệu và giao diện.

## 4. Vì sao `Driving::isBusy()` là `false`?

Đây là điểm dễ gây hiểu nhầm nhất. Trong thiết kế này, `isBusy()` không có nghĩa tổng quát là "robot có đang chuyển động hay không". Nó có nghĩa gần hơn với:

> Có một tác vụ bất đồng bộ độc quyền đang chờ response hay không?

Vì vậy:

- `JointBusy::isBusy() == true`: đang có một lệnh joint/pose chưa nhận được kết quả.
- `Driving::isBusy() == false`: không có giao dịch một lần nào đang chờ hoàn tất; robot đang ở chế độ điều khiển liên tục.

Mặc dù `isBusy()` bằng `false`, `Driving` vẫn được bảo vệ bằng state riêng. State này chỉ cho phép cập nhật lệnh drive hoặc gọi Stop. Các thao tác joint/pose dùng implementation mặc định của `RobotState` và sẽ bị từ chối.

Controller cũng không cho response status chạy nền tự động ghi đè cả `JointBusy` lẫn `Driving`:

```cpp
!state_->isBusy()
&& state_->name() != QStringLiteral("Driving")
```

Hai state được bảo vệ vì hai nguyên nhân khác nhau:

- Không ghi đè `JointBusy` vì ứng dụng đang chờ kết quả của một operation cụ thể.
- Không ghi đè `Driving` vì chế độ lái phải kết thúc bằng Stop, không phải bởi response status chạy nền.

## 5. Vì sao không gộp thành một state `Busy` hoặc `Moving`?

Nếu gộp hai state, state chung sẽ phải chứa nhiều nhánh điều kiện:

```cpp
if (busyType == Driving)
{
    // Cho phép đổi velocity và kết thúc khi Stop.
}
else if (busyType == JointCommand)
{
    // Khóa velocity và kết thúc khi nhận đúng response.
}
```

Sau đó, các hàm `onResponse()`, `startDrive()`, `stopDrive()`, timer và giao diện đều phải tiếp tục kiểm tra `busyType`. Khi đó State Pattern gần như mất ý nghĩa: thay vì phân phối hành vi bằng đa hình theo state, chương trình lại tạo một state lớn chứa nhiều `if/else`.

Việc tách hai state mang lại các lợi ích sau.

### 5.1. Mỗi state có điều kiện kết thúc rõ ràng

- `Driving` kết thúc bởi `stopDrive()`.
- `JointBusy` kết thúc bởi response khớp với `pendingOperation_`.

### 5.2. Ngăn các lệnh chuyển động xung đột

- Khi `Driving`, người dùng có thể cập nhật velocity nhưng không thể gửi joint/pose.
- Khi `JointBusy`, người dùng không thể gửi thêm velocity, joint hoặc pose.

### 5.3. Không gửi trùng lệnh joint

Nếu coi joint giống driving và gửi lặp theo timer, một lần nhấn có thể tạo ra hàng loạt trajectory hoặc `joint_nudge`, làm chuyển động bị cộng dồn ngoài ý muốn.

### 5.4. Không kết thúc `Driving` vì response không liên quan

`Driving` không chờ một response hoàn tất. Response status hoặc velocity acknowledgment không có nghĩa người dùng đã yêu cầu robot dừng.

### 5.5. Đồng bộ chính xác response của joint/pose

`JointBusy` cần nhớ operation đang chờ. Điều này tránh việc một response nền, response cũ hoặc response của thao tác khác làm state quay về `Ready` quá sớm.

### 5.6. Giao diện phản ánh đúng quyền thao tác

- `Driving` có `canDrive() == true`, vì vậy nhóm điều khiển đế vẫn hoạt động để người dùng đổi hướng hoặc dừng.
- `JointBusy` có `isBusy() == true` và các capability điều khiển mặc định đều là `false`, vì vậy UI khóa cả nhóm drive và joint cho đến khi thao tác hoàn tất.

## 6. Kết luận

`Driving` và `JointBusy` không được tách ra chỉ vì chúng điều khiển hai bộ phận khác nhau. Lý do quan trọng hơn là chúng có hai mô hình thực thi khác nhau:

```text
Driving:
lệnh liên tục → được cập nhật nhiều lần → người dùng Stop → Ready

JointBusy:
lệnh một lần → khóa lệnh xung đột → chờ đúng response → Ready
```

Có thể tổng kết câu trả lời như sau:

> `Driving` là state điều khiển đế theo kiểu streaming: lệnh velocity được gửi lặp lại, có thể cập nhật trong khi đang lái và chỉ kết thúc khi người dùng ra lệnh Stop. `JointBusy` là state chờ hoàn tất một thao tác joint/pose bất đồng bộ: lệnh chỉ được gửi một lần, các lệnh chuyển động mới bị khóa và state chỉ kết thúc khi bridge trả response đúng với operation đang chờ. Hai state cần được tách vì chúng khác nhau về tập lệnh được phép, cơ chế truyền lệnh, điều kiện hoàn tất và cách xử lý response. Nếu gộp lại, code phải sử dụng nhiều cờ và `if/else`, làm mất lợi ích của State Pattern và tăng nguy cơ gửi lệnh xung đột hoặc chuyển state sai.
