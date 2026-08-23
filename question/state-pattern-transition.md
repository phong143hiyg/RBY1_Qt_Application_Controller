# Chuyển trạng thái trong State Pattern

## Câu hỏi

> Khi cần chuyển state thì mô hình State Pattern sẽ làm như thế nào để đổi trạng thái?

## Trả lời

Khi cần chuyển trạng thái, **Concrete State hiện tại sẽ xác định State tiếp theo**, sau đó **Context thay thế đối tượng State đang giữ bằng đối tượng State mới**.

Trong project này, `RobotController` là Context và lưu trạng thái hiện tại bằng:

```cpp
std::unique_ptr<RobotState> state_;
```

Khi xử lý một sự kiện, State hiện tại trả về State tiếp theo. Ví dụ, khi robot đang ở `ReadyState` và nhận lệnh chạy:

```cpp
std::unique_ptr<RobotState> ReadyState::startDrive(
    RobotController &controller,
    double linearX,
    double linearY,
    double angularZ)
{
    controller.startVelocityInternal(
        linearX,
        linearY,
        angularZ);

    return std::make_unique<DrivingState>();
}
```

`RobotController` nhận State mới và thực hiện chuyển đổi:

```cpp
void RobotController::applyTransition(
    std::unique_ptr<RobotState> nextState)
{
    if (nextState)
    {
        transitionTo(std::move(nextState));
    }
}

void RobotController::transitionTo(
    std::unique_ptr<RobotState> nextState)
{
    if (!nextState)
    {
        return;
    }

    state_ = std::move(nextState);
    emitCurrentState();
}
```

Quy ước chuyển trạng thái của project:

```text
Trả về State mới  -> chuyển sang trạng thái mới
Trả về nullptr    -> giữ nguyên trạng thái hiện tại
```

Ví dụ luồng chuyển trạng thái:

```text
Ready --startDrive()--> Driving
Driving --stopDrive()--> Ready
```

Nhờ sử dụng `std::unique_ptr`, khi `state_` nhận đối tượng State mới thì đối tượng State cũ được tự động hủy, không cần gọi `delete` thủ công.

## Câu trả lời ngắn

> State Pattern đổi trạng thái bằng cách thay thế đối tượng State hiện tại bên trong Context bằng một đối tượng State mới. Trong project, Concrete State trả về `std::unique_ptr<RobotState>` chứa State tiếp theo; `RobotController` dùng `std::move` để gán nó cho `state_`. Nếu State trả về `nullptr` thì trạng thái được giữ nguyên.
