# Trình bày State Pattern và các tính chất C++ OOP được sử dụng

## Câu hỏi

> Trình bày State Pattern và phân tích chi tiết State Pattern sử dụng tính chất nào của C++ OOP để tạo nên pattern này. Có thể trình bày luôn bằng code tay đối với các pattern trong project này của em.

## Trả lời

## 1. State Pattern là gì?

State Pattern là một mẫu thiết kế hành vi (Behavioral Design Pattern). Pattern này cho phép một đối tượng thay đổi hành vi khi trạng thái bên trong của nó thay đổi.

Thay vì viết một hàm lớn gồm nhiều `if/else` hoặc `switch`:

```cpp
if (state == READY)
{
    // Xử lý drive.
}
else if (state == DRIVING)
{
    // Cập nhật velocity.
}
else if (state == JOINT_BUSY)
{
    // Từ chối drive.
}
```

State Pattern tách hành vi của từng trạng thái thành một class riêng:

```text
RobotState
├── DisconnectedState
├── ConnectedState
├── PreparingState
├── ReadyState
├── DrivingState
└── JointBusyState
```

`RobotController` chỉ giữ state hiện tại và chuyển lời gọi cho state đó xử lý:

```cpp
state_->startDrive(*this, x, y, z);
```

Nhờ đa hình, cùng một lời gọi `startDrive()` nhưng kết quả phụ thuộc vào đối tượng thực tế mà `state_` đang trỏ tới.

## 2. Các thành phần State Pattern trong project

| Thành phần State Pattern | Class trong project | Vai trò |
|---|---|---|
| Context | `RobotController` | Quản lý robot và giữ state hiện tại |
| State | `RobotState` | Khai báo giao diện chung cho các trạng thái |
| Concrete State | `DisconnectedState`, `ConnectedState`, `PreparingState`, `ReadyState`, `DrivingState`, `JointBusyState` | Cài đặt hành vi riêng của từng trạng thái |
| Client | `MainWindow` | Gửi yêu cầu điều khiển cho `RobotController` |

Context lưu trạng thái hiện tại bằng:

```cpp
std::unique_ptr<RobotState> state_;
```

State ban đầu là `DisconnectedState`:

```cpp
RobotController::RobotController(QObject *parent)
    : QObject(parent),
      client_(new RobotClient(this)),
      state_(std::make_unique<DisconnectedState>())
{
}
```

## 3. Luồng trạng thái trong project

```text
Disconnected
    │ Connect thành công
    ▼
Connected
    │ Prepare
    ▼
Preparing
    ├── thành công ──> Ready
    └── thất bại ───> Connected

Ready
    ├── Drive ──────> Driving
    │                   │ Stop
    │                   └──────> Ready
    │
    └── Joint/Pose ─> JointBusy
                        │ nhận response
                        └────────> Ready
```

Ngoài luồng trên:

- Mất kết nối: chuyển về `Disconnected`.
- Cancel: chuyển về `Connected`.
- Lệnh không hợp lệ: giữ nguyên state.
- State trả về `nullptr`: không thực hiện chuyển trạng thái.

## 4. State Pattern sử dụng tính chất OOP nào?

Tính chất quan trọng nhất là **đa hình động**, nhưng pattern được tạo nên từ sự kết hợp của trừu tượng, kế thừa, đa hình, đóng gói và composition.

### 4.1. Tính trừu tượng (Abstraction)

`RobotState` mô tả những hành vi mà một trạng thái robot có thể xử lý:

```cpp
class RobotState
{
public:
    virtual ~RobotState() = default;

    virtual QString name() const = 0;

    virtual std::unique_ptr<RobotState> prepare(
        RobotController &controller);

    virtual std::unique_ptr<RobotState> startDrive(
        RobotController &controller,
        double linearX,
        double linearY,
        double angularZ);

    virtual std::unique_ptr<RobotState> stopDrive(
        RobotController &controller);

    virtual std::unique_ptr<RobotState> nudgeJoint(
        RobotController &controller,
        const QString &groupName,
        int jointIndex,
        double delta,
        double minimumTime);

    virtual std::unique_ptr<RobotState> sendPose(
        RobotController &controller,
        const QString &command,
        const QString &operationName,
        double minimumTime);

    virtual std::unique_ptr<RobotState> onResponse(
        RobotController &controller,
        const QString &operationName,
        const QJsonObject &response);
};
```

`RobotController` chỉ cần biết giao diện `RobotState`, không cần biết chi tiết từng state xử lý lệnh như thế nào.

Trong project, `name()` là pure virtual:

```cpp
virtual QString name() const = 0;
```

Vì vậy `RobotState` là abstract class và không thể tạo trực tiếp:

```cpp
RobotState state; // Không hợp lệ.
```

### 4.2. Tính kế thừa (Inheritance)

Các state cụ thể kế thừa từ `RobotState`:

```cpp
class ReadyState final : public RobotState
{
    // Cài đặt hành vi của trạng thái Ready.
};

class DrivingState final : public RobotState
{
    // Cài đặt hành vi của trạng thái Driving.
};
```

Nhờ kế thừa, mọi state đều có chung kiểu cơ sở là `RobotState`. Vì vậy `RobotController` có thể lưu bất kỳ state nào bằng:

```cpp
std::unique_ptr<RobotState> state_;
```

Từ khóa `final` cho biết class đó không được tiếp tục làm lớp cha. `final` không bắt buộc đối với State Pattern, nhưng giúp thiết kế rõ ràng hơn.

### 4.3. Tính đa hình (Polymorphism)

Đây là tính chất cốt lõi nhất của State Pattern.

Controller luôn gọi cùng một câu lệnh:

```cpp
state_->startDrive(*this, x, y, z);
```

Tuy nhiên, hành vi thực tế phụ thuộc vào state hiện tại.

#### Khi state là `ReadyState`

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

Robot bắt đầu di chuyển và chuyển sang `DrivingState`.

#### Khi state là `DrivingState`

```cpp
std::unique_ptr<RobotState> DrivingState::startDrive(
    RobotController &controller,
    double linearX,
    double linearY,
    double angularZ)
{
    controller.startVelocityInternal(
        linearX,
        linearY,
        angularZ);

    return nullptr;
}
```

Velocity được cập nhật nhưng state vẫn là `Driving`.

#### Khi state là `ConnectedState`

`ConnectedState` không override `startDrive()`, nên implementation mặc định của `RobotState` được gọi:

```cpp
std::unique_ptr<RobotState> RobotState::startDrive(
    RobotController &controller,
    double,
    double,
    double)
{
    controller.rejectAction(
        QStringLiteral(
            "Không thể điều khiển đế trong trạng thái %1.")
            .arg(name()));

    return nullptr;
}
```

Lệnh bị từ chối và trạng thái không thay đổi.

Như vậy, cùng một lời gọi cho ba hành vi khác nhau:

```text
Ready      → bắt đầu drive, chuyển sang Driving
Driving    → cập nhật velocity, giữ nguyên Driving
Connected  → từ chối, giữ nguyên Connected
```

Đây là dynamic polymorphism, được C++ thực hiện thông qua hàm `virtual`. Về cách triển khai, compiler thường sử dụng virtual table, nhưng tiêu chuẩn C++ không bắt buộc phải triển khai bằng vtable.

Đây là **overriding**, không phải overloading:

- Overriding: class con cài đặt lại hàm `virtual` của class cha.
- Overloading: nhiều hàm cùng tên nhưng khác danh sách tham số.

### 4.4. Tính đóng gói (Encapsulation)

Mỗi state đóng gói:

- Những lệnh nào được phép.
- Cách xử lý lệnh.
- Điều kiện chuyển sang state khác.
- Dữ liệu riêng của state.

Ví dụ `JointBusyState` cần nhớ thao tác đang chờ:

```cpp
class JointBusyState final : public RobotState
{
private:
    QString pendingOperation_;
};
```

Khi nhận response, state chỉ xử lý response đúng với thao tác đó:

```cpp
if (operationName != pendingOperation_)
{
    return nullptr;
}

return std::make_unique<ReadyState>();
```

Các class khác không được truy cập trực tiếp `pendingOperation_` vì nó là `private`. Điều này ngăn một response không liên quan làm robot chuyển về `Ready` quá sớm.

### 4.5. Composition — quan hệ “has-a”

`RobotController` không kế thừa `RobotState`. Controller chứa một state:

```cpp
class RobotController
{
private:
    std::unique_ptr<RobotState> state_;
};
```

Quan hệ này được gọi là composition:

```text
RobotController has a RobotState
```

Khi trạng thái thay đổi, controller thay đối tượng state đang được chứa:

```cpp
state_ = std::move(nextState);
```

Kế thừa được sử dụng giữa `RobotState` và các concrete state; composition được sử dụng giữa `RobotController` và `RobotState`.

## 5. Các tính chất riêng của C++ hỗ trợ pattern

### 5.1. `virtual` và `override`

`virtual` bật đa hình động:

```cpp
virtual std::unique_ptr<RobotState> startDrive(...);
```

`override` yêu cầu compiler kiểm tra hàm class con có thật sự ghi đè đúng hàm class cha hay không:

```cpp
std::unique_ptr<RobotState> startDrive(...) override;
```

Nếu viết sai kiểu hoặc số lượng tham số, compiler sẽ báo lỗi.

### 5.2. Virtual destructor

```cpp
virtual ~RobotState() = default;
```

Controller hủy object concrete thông qua con trỏ base `RobotState`. Vì vậy destructor của lớp cơ sở phải là `virtual` để destructor đúng của `ReadyState`, `DrivingState` hoặc state tương ứng được gọi.

### 5.3. `std::unique_ptr`

`unique_ptr` thể hiện controller là chủ sở hữu duy nhất của state hiện tại:

```cpp
std::unique_ptr<RobotState> state_;
```

Khi chuyển state:

```cpp
state_ = std::move(nextState);
```

State cũ được tự động hủy, không cần gọi `delete` và hạn chế memory leak.

### 5.4. `std::make_unique`

State mới được tạo bằng:

```cpp
return std::make_unique<DrivingState>();
```

Cách này an toàn và thể hiện quyền sở hữu rõ hơn so với việc quản lý con trỏ thô bằng `new` và `delete`.

### 5.5. Tham chiếu `RobotController&`

State nhận controller bằng tham chiếu:

```cpp
startDrive(RobotController &controller, ...);
```

State có thể gọi các thao tác nội bộ của controller. Tham chiếu cũng thể hiện controller bắt buộc phải tồn tại và không được là `nullptr`.

## 6. Cơ chế chuyển trạng thái trong project

Controller chuyển tiếp yêu cầu cho state hiện tại:

```cpp
void RobotController::startDrive(
    double linearX,
    double linearY,
    double angularZ)
{
    applyTransition(
        state_->startDrive(
            *this,
            linearX,
            linearY,
            angularZ));
}
```

State trả về state tiếp theo:

```cpp
return std::make_unique<DrivingState>();
```

Controller nhận và thay thế state hiện tại:

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

Quy ước của project:

```text
Trả về state mới → chuyển trạng thái
Trả về nullptr   → giữ nguyên trạng thái
```

## 7. Code tay rút gọn

Đoạn code sau minh họa phần quan trọng nhất của pattern và có thể dùng để viết tay khi trình bày:

```cpp
#include <iostream>
#include <memory>

class RobotController;

class RobotState
{
public:
    virtual ~RobotState() = default;

    virtual const char *name() const = 0;

    virtual std::unique_ptr<RobotState> startDrive(
        RobotController &controller);

    virtual std::unique_ptr<RobotState> stopDrive(
        RobotController &controller);
};

class ReadyState final : public RobotState
{
public:
    const char *name() const override
    {
        return "Ready";
    }

    std::unique_ptr<RobotState> startDrive(
        RobotController &controller) override;
};

class DrivingState final : public RobotState
{
public:
    const char *name() const override
    {
        return "Driving";
    }

    std::unique_ptr<RobotState> startDrive(
        RobotController &controller) override;

    std::unique_ptr<RobotState> stopDrive(
        RobotController &controller) override;
};

class RobotController
{
private:
    std::unique_ptr<RobotState> state_;

public:
    RobotController()
        : state_(std::make_unique<ReadyState>())
    {
    }

    void sendVelocity()
    {
        std::cout << "Gui velocity\n";
    }

    void sendStop()
    {
        std::cout << "Gui stop\n";
    }

    void applyTransition(
        std::unique_ptr<RobotState> nextState)
    {
        if (nextState)
        {
            std::cout << state_->name()
                      << " -> "
                      << nextState->name()
                      << '\n';

            state_ = std::move(nextState);
        }
    }

    void startDrive()
    {
        applyTransition(state_->startDrive(*this));
    }

    void stopDrive()
    {
        applyTransition(state_->stopDrive(*this));
    }
};

std::unique_ptr<RobotState> RobotState::startDrive(
    RobotController &)
{
    std::cout << "State hien tai khong cho phep drive\n";
    return nullptr;
}

std::unique_ptr<RobotState> RobotState::stopDrive(
    RobotController &)
{
    return nullptr;
}

std::unique_ptr<RobotState> ReadyState::startDrive(
    RobotController &controller)
{
    controller.sendVelocity();
    return std::make_unique<DrivingState>();
}

std::unique_ptr<RobotState> DrivingState::startDrive(
    RobotController &controller)
{
    controller.sendVelocity();
    return nullptr; // Vẫn ở Driving.
}

std::unique_ptr<RobotState> DrivingState::stopDrive(
    RobotController &controller)
{
    controller.sendStop();
    return std::make_unique<ReadyState>();
}
```

Luồng của code trên:

```text
Ready --startDrive()--> Driving
Driving --startDrive()--> Driving
Driving --stopDrive()--> Ready
```

## 8. Lợi ích so với enum và switch

Nếu không dùng State Pattern, controller có thể phải kiểm tra state trong nhiều hàm:

```cpp
switch (state)
{
case READY:
    // Xử lý cho Ready.
    break;

case DRIVING:
    // Xử lý cho Driving.
    break;

case JOINT_BUSY:
    // Xử lý cho JointBusy.
    break;
}
```

Khi số lượng lệnh và trạng thái tăng, mỗi hàm đều phải kiểm tra state. Điều này tạo ra nhiều `if/else`, dễ thiếu trường hợp và khó bảo trì.

State Pattern mang lại các lợi ích:

- Mỗi state có trách nhiệm rõ ràng.
- Hạn chế `if/else` và `switch` trong controller.
- Lệnh không hợp lệ được kiểm soát theo state.
- Chuyển trạng thái được thể hiện rõ trong code.
- Dễ thêm trạng thái mới mà ít ảnh hưởng code cũ.
- UI có thể lấy capability như `canDrive()` hoặc `isBusy()` từ state.

Đổi lại, pattern tạo ra nhiều class và file hơn. Vì vậy nó phù hợp khi đối tượng có nhiều trạng thái và hành vi thay đổi đáng kể theo trạng thái.

## 9. Nhận xét về implementation trong project

Phần xử lý các lệnh như `prepare()`, `startDrive()`, `stopDrive()`, `nudgeJoint()`, `sendPose()` và `onResponse()` đã sử dụng đúng ý tưởng State Pattern: controller ủy quyền hành vi cho state hiện tại thông qua hàm `virtual`.

Một số sự kiện dùng chung như mất kết nối, Cancel và đồng bộ status vẫn được xử lý trực tiếp trong `RobotController`. Đây là lựa chọn thực tế vì các sự kiện này có tính toàn cục. Nếu muốn áp dụng State Pattern thuần hơn, có thể bổ sung các hàm như `onDisconnected()`, `cancel()` hoặc `onStatus()` vào `RobotState` để hạn chế việc controller kiểm tra tên state.

Việc kiểm tra bằng chuỗi:

```cpp
state_->name() != QStringLiteral("Driving")
```

cũng có thể được thay bằng một capability ảo, ví dụ:

```cpp
virtual bool acceptsStatusTransition() const
{
    return true;
}
```

Sau đó `DrivingState` và các busy state override thành `false`. Cách này tiếp tục sử dụng đa hình thay vì phụ thuộc vào tên trạng thái.

## 10. Kết luận ngắn để trả lời giảng viên

> State Pattern là Behavioral Design Pattern dùng để tách hành vi phụ thuộc trạng thái thành các class riêng. Trong project, `RobotController` là Context, `RobotState` là abstract State và các class `DisconnectedState`, `ConnectedState`, `PreparingState`, `ReadyState`, `DrivingState`, `JointBusyState` là Concrete State. Tính chất OOP cốt lõi được sử dụng là đa hình động thông qua hàm `virtual`: controller gọi cùng một hàm qua con trỏ `RobotState`, nhưng C++ tự chọn implementation của state thực tế tại runtime. Pattern còn sử dụng trừu tượng qua lớp `RobotState`, kế thừa và overriding ở các concrete state, đóng gói hành vi và dữ liệu riêng trong từng state, đồng thời sử dụng composition vì `RobotController` chứa một `unique_ptr<RobotState>`. `unique_ptr`, virtual destructor, `override`, `make_unique` và move semantics là các cơ chế C++ giúp việc quản lý và chuyển state an toàn.

Có thể tóm tắt ngắn nhất như sau:

```text
State Pattern = Abstraction + Inheritance + Encapsulation
                + Dynamic Polymorphism + Composition

Trong đó Dynamic Polymorphism là cơ chế cốt lõi.
```
