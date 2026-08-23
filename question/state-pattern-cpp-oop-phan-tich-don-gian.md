# Phân tích State Pattern qua các tính chất OOP của C++

## 1. Ý tưởng đơn giản nhất

State Pattern dùng khi **một đối tượng có nhiều trạng thái và hành vi của nó thay đổi theo trạng thái hiện tại**.

Trong project này, đối tượng chính là `RobotController`. Robot có các trạng thái như:

```text
Disconnected -> Connected -> Preparing -> Ready
                                      Ready -> Driving
                                      Ready -> JointBusy
```

Ví dụ, cùng là lệnh `startDrive()` nhưng:

- Ở `Ready`: robot được chạy và chuyển sang `Driving`.
- Ở `Driving`: robot chỉ cập nhật vận tốc, vẫn ở `Driving`.
- Ở `Connected`: robot chưa sẵn sàng nên lệnh bị từ chối.

Nếu chỉ dùng `enum`, code thường phải kiểm tra trạng thái bằng nhiều `if/else` hoặc `switch`:

```cpp
void RobotController::startDrive()
{
    if (state == Ready)
    {
        // Bắt đầu chạy.
    }
    else if (state == Driving)
    {
        // Cập nhật vận tốc.
    }
    else
    {
        // Từ chối lệnh.
    }
}
```

State Pattern thay mỗi giá trị trạng thái bằng **một object trạng thái**. Mỗi object tự chứa cách xử lý phù hợp với trạng thái của nó.

```text
RobotController
      |
      | chứa state hiện tại
      v
  RobotState
      ^
      |
      +-- ReadyState
      +-- DrivingState
      +-- ConnectedState
      +-- JointBusyState
      +-- ...
```

## 2. Các vai trò trong project

| Vai trò trong State Pattern | Thành phần trong project | Nhiệm vụ |
|---|---|---|
| Context | `RobotController` | Nhận yêu cầu từ bên ngoài và giữ trạng thái hiện tại |
| State | `RobotState` | Định nghĩa giao diện chung cho mọi trạng thái |
| Concrete State | `ReadyState`, `DrivingState`, `ConnectedState`, `JointBusyState`,... | Cài đặt hành vi riêng của từng trạng thái |
| Client | `MainWindow` | Gọi các chức năng của `RobotController` |

`RobotController` không cần chứa toàn bộ luật xử lý của từng trạng thái. Nó chuyển yêu cầu cho state hiện tại:

```cpp
applyTransition(
    state_->startDrive(
        *this,
        linearX,
        linearY,
        angularZ));
```

## 3. State Pattern dùng những tính chất OOP nào?

Có thể tóm tắt như sau:

```text
State Pattern
    = Trừu tượng
    + Kế thừa
    + Đa hình động
    + Đóng gói
    + Composition
```

Trong đó, **đa hình động là cơ chế cốt lõi**, còn **composition là cách `RobotController` thay đổi hành vi bằng cách thay object state đang chứa**.

### 3.1. Tính trừu tượng (Abstraction)

Trừu tượng nghĩa là chỉ đưa ra những thao tác cần thiết, còn che đi chi tiết mỗi thao tác được thực hiện như thế nào.

Trong project, `RobotState` là lớp trừu tượng chung:

```cpp
class RobotState
{
public:
    virtual ~RobotState() = default;

    virtual QString name() const = 0;

    virtual std::unique_ptr<RobotState> startDrive(
        RobotController &controller,
        double linearX,
        double linearY,
        double angularZ);

    virtual std::unique_ptr<RobotState> stopDrive(
        RobotController &controller);
};
```

Lớp này trả lời câu hỏi: “Một trạng thái robot có thể nhận những yêu cầu nào?”. Nó không bắt `RobotController` phải biết `ReadyState` hay `DrivingState` xử lý chi tiết ra sao.

Hàm:

```cpp
virtual QString name() const = 0;
```

là **pure virtual function**, vì có `= 0`. Do đó `RobotState` là abstract class và không thể tạo object trực tiếp:

```cpp
RobotState state; // Lỗi biên dịch.
```

Ý nghĩa đối với State Pattern: mọi trạng thái có một “hợp đồng” chung để `RobotController` sử dụng.

### 3.2. Tính kế thừa (Inheritance)

Các trạng thái cụ thể kế thừa `RobotState`:

```cpp
class ReadyState final : public RobotState
{
public:
    QString name() const override;

    std::unique_ptr<RobotState> startDrive(
        RobotController &controller,
        double linearX,
        double linearY,
        double angularZ) override;
};
```

```cpp
class DrivingState final : public RobotState
{
public:
    QString name() const override;

    std::unique_ptr<RobotState> startDrive(
        RobotController &controller,
        double linearX,
        double linearY,
        double angularZ) override;

    std::unique_ptr<RobotState> stopDrive(
        RobotController &controller) override;
};
```

Nhờ quan hệ “is-a”:

```text
ReadyState   is a RobotState
DrivingState is a RobotState
```

`RobotController` có thể giữ mọi trạng thái cụ thể bằng cùng một kiểu:

```cpp
std::unique_ptr<RobotState> state_;
```

Từ khóa `override` giúp compiler kiểm tra class con có ghi đè đúng chữ ký hàm của class cha hay không. `final` ngăn không cho tiếp tục kế thừa class trạng thái cụ thể; nó giúp thiết kế rõ ràng nhưng không phải điều kiện bắt buộc của State Pattern.

### 3.3. Tính đa hình động (Runtime Polymorphism)

Đây là phần quan trọng nhất.

Biến `state_` có kiểu khai báo là:

```cpp
std::unique_ptr<RobotState>
```

nhưng object thực tế bên trong có thể là `ReadyState`, `DrivingState` hoặc một state khác. Khi gọi:

```cpp
state_->startDrive(*this, x, y, z);
```

C++ chọn hàm cần chạy dựa trên **kiểu thực tế của object tại runtime**, vì `startDrive()` là hàm `virtual`.

#### Trường hợp object thực tế là `ReadyState`

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

Kết quả: gửi vận tốc và yêu cầu chuyển từ `Ready` sang `Driving`.

#### Trường hợp object thực tế là `DrivingState`

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

Kết quả: cập nhật vận tốc nhưng không chuyển state.

#### Trường hợp state không ghi đè `startDrive()`

Ví dụ `ConnectedState` không ghi đè hàm này, nên implementation mặc định trong `RobotState` được dùng:

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

Kết quả: lệnh bị từ chối và state được giữ nguyên.

Như vậy, một lời gọi có ba hành vi:

| Object mà `state_` đang giữ | Hàm thực sự chạy | Kết quả |
|---|---|---|
| `ReadyState` | `ReadyState::startDrive()` | Chạy và chuyển sang `Driving` |
| `DrivingState` | `DrivingState::startDrive()` | Cập nhật vận tốc, giữ nguyên state |
| `ConnectedState` | `RobotState::startDrive()` | Từ chối, giữ nguyên state |

Đây là **overriding**, không phải overloading:

- Overriding: class con cài đặt lại một hàm `virtual` của class cha.
- Overloading: nhiều hàm trùng tên nhưng khác danh sách tham số.

### 3.4. Tính đóng gói (Encapsulation)

Mỗi concrete state đóng gói ba thứ:

1. Lệnh nào hợp lệ trong state đó.
2. Cách xử lý lệnh.
3. Khi nào cần chuyển sang state khác.

State cũng có thể giữ dữ liệu riêng. Ví dụ `JointBusyState` ghi nhớ thao tác đang chờ:

```cpp
class JointBusyState final : public RobotState
{
private:
    QString pendingOperation_;
};
```

Khi có response, nó chỉ phản ứng với đúng operation:

```cpp
if (operationName != pendingOperation_)
{
    return nullptr;
}

return std::make_unique<ReadyState>();
```

`pendingOperation_` là `private`, nên object khác không thể tùy ý sửa nó. Dữ liệu và luật xử lý của trạng thái bận được đặt chung trong `JointBusyState` thay vì rải rác trong `RobotController`.

### 3.5. Composition — quan hệ “has-a”

Composition không phải một trong bốn tính chất OOP cơ bản thường được học, nhưng nó là quan hệ thiết kế rất quan trọng để tạo nên State Pattern.

`RobotController` **không phải** là một `RobotState`. Nó **có** một `RobotState`:

```cpp
class RobotController
{
private:
    std::unique_ptr<RobotState> state_;
};
```

```text
RobotController has a RobotState
```

Khi trạng thái thay đổi, controller không tự biến thành class khác. Nó thay object đang được `state_` sở hữu:

```cpp
state_ = std::move(nextState);
```

Phân biệt hai quan hệ trong pattern:

```text
Kế thừa:
ReadyState -------- is-a --------> RobotState

Composition:
RobotController ---- has-a -------> RobotState
```

## 4. Cơ chế chuyển trạng thái trong project

Quá trình chuyển state gồm ba bước.

### Bước 1: Controller chuyển tiếp yêu cầu

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

### Bước 2: State xử lý và trả về kết quả chuyển trạng thái

```cpp
return std::make_unique<DrivingState>(); // Muốn chuyển state.
```

hoặc:

```cpp
return nullptr; // Muốn giữ nguyên state.
```

### Bước 3: Controller thay state hiện tại

```cpp
void RobotController::applyTransition(
    std::unique_ptr<RobotState> nextState)
{
    if (nextState)
    {
        transitionTo(std::move(nextState));
    }
}
```

```cpp
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

Quy ước quan trọng của project:

```text
State trả object mới -> chuyển sang state mới
State trả nullptr    -> giữ nguyên state hiện tại
```

Ở implementation này, concrete state quyết định state tiếp theo, còn `RobotController` chịu trách nhiệm sở hữu và thực hiện việc thay thế.

## 5. Các đặc điểm C++ hỗ trợ pattern

### `virtual`

Cho phép gọi đúng implementation của object thực tế tại runtime. Không có `virtual`, lời gọi qua `RobotState*` sẽ không tạo được đa hình động như mong muốn.

### `override`

Cho compiler kiểm tra class con có ghi đè đúng hàm của class cha. Nếu sai kiểu tham số, thiếu `const` hoặc sai kiểu trả về, compiler sẽ báo lỗi.

### Virtual destructor

```cpp
virtual ~RobotState() = default;
```

Object cụ thể được hủy qua con trỏ lớp cha `RobotState`. Destructor ảo bảo đảm destructor của class con được gọi đúng.

### `std::unique_ptr`

```cpp
std::unique_ptr<RobotState> state_;
```

Nó thể hiện `RobotController` sở hữu duy nhất state hiện tại. Khi gán state mới, state cũ được hủy tự động, tránh phải quản lý `new`/`delete` thủ công.

### `std::make_unique`

```cpp
return std::make_unique<DrivingState>();
```

Tạo object state mới và trả quyền sở hữu một cách rõ ràng, an toàn.

### Move semantics

`unique_ptr` không được copy vì chỉ có một chủ sở hữu. Quyền sở hữu state mới được chuyển cho controller bằng:

```cpp
state_ = std::move(nextState);
```

Sau lệnh này, `state_` sở hữu object; `nextState` không còn sở hữu object đó.

### Tham chiếu `RobotController&`

```cpp
startDrive(RobotController &controller, ...)
```

State cần dùng controller để gọi các thao tác thật như gửi vận tốc hoặc ghi log. Dùng tham chiếu cho biết controller phải tồn tại và không thể là `nullptr`.

## 6. Vì sao State Pattern tốt hơn một `switch` lớn?

| Dùng `enum` và `switch` | Dùng State Pattern |
|---|---|
| Luật của nhiều state nằm chung trong controller | Luật của mỗi state nằm trong class riêng |
| Thêm state thường phải sửa nhiều `switch` | Có thể thêm một concrete state mới |
| Controller biết quá nhiều chi tiết | Controller làm việc qua `RobotState` |
| Dễ bỏ sót một nhánh xử lý | Hành vi mặc định có thể đặt ở base state |
| Ít class, phù hợp bài toán nhỏ | Nhiều class hơn, phù hợp máy trạng thái phức tạp |

State Pattern không phải lúc nào cũng tốt hơn `enum`. Nếu chỉ có hai trạng thái và rất ít hành vi, một `if` đơn giản có thể dễ hiểu hơn. Pattern có giá trị khi số state, số lệnh và luật chuyển state đủ lớn.

## 7. Ví dụ C++ rút gọn để dễ nhớ

```cpp
#include <iostream>
#include <memory>

class Controller;

class State
{
public:
    virtual ~State() = default;
    virtual const char *name() const = 0;
    virtual std::unique_ptr<State> drive(Controller &controller);
    virtual std::unique_ptr<State> stop(Controller &controller);
};

class DrivingState;

class ReadyState final : public State
{
public:
    const char *name() const override { return "Ready"; }
    std::unique_ptr<State> drive(Controller &controller) override;
};

class DrivingState final : public State
{
public:
    const char *name() const override { return "Driving"; }
    std::unique_ptr<State> drive(Controller &controller) override;
    std::unique_ptr<State> stop(Controller &controller) override;
};

class Controller
{
private:
    std::unique_ptr<State> state_ = std::make_unique<ReadyState>();

public:
    void sendVelocity() { std::cout << "Send velocity\n"; }
    void sendStop() { std::cout << "Send stop\n"; }

    void changeState(std::unique_ptr<State> nextState)
    {
        if (nextState)
        {
            state_ = std::move(nextState);
        }
    }

    void drive()
    {
        changeState(state_->drive(*this));
    }

    void stop()
    {
        changeState(state_->stop(*this));
    }
};

std::unique_ptr<State> State::drive(Controller &)
{
    std::cout << "Drive is not allowed in this state\n";
    return nullptr;
}

std::unique_ptr<State> State::stop(Controller &)
{
    return nullptr;
}

std::unique_ptr<State> ReadyState::drive(Controller &controller)
{
    controller.sendVelocity();
    return std::make_unique<DrivingState>();
}

std::unique_ptr<State> DrivingState::drive(Controller &controller)
{
    controller.sendVelocity();
    return nullptr; // Vẫn ở Driving.
}

std::unique_ptr<State> DrivingState::stop(Controller &controller)
{
    controller.sendStop();
    return std::make_unique<ReadyState>();
}
```

Luồng hoạt động:

```text
Ready --drive()--> Driving
Driving --drive()--> Driving
Driving --stop()--> Ready
```

## 8. Cách trình bày ngắn khi được hỏi

> State Pattern tách hành vi phụ thuộc trạng thái thành các class riêng. Trong project, `RobotController` là Context, `RobotState` là lớp State trừu tượng, còn `ReadyState`, `DrivingState`, `JointBusyState`,... là các Concrete State. Pattern dùng trừu tượng để tạo giao diện chung, dùng kế thừa để các state có chung kiểu `RobotState`, dùng đóng gói để mỗi state giữ luật và dữ liệu riêng, và đặc biệt dùng đa hình động qua hàm `virtual` để cùng một lời gọi có hành vi khác nhau tùy object state tại runtime. `RobotController` dùng composition vì nó chứa một `unique_ptr<RobotState>` và thay object này khi chuyển trạng thái. `unique_ptr`, virtual destructor, `override`, `make_unique` và move semantics giúp cài đặt pattern an toàn trong C++.

Một câu dễ nhớ nhất:

```text
Thay vì hỏi “đang ở state nào?” bằng switch,
Controller giao việc cho chính object state hiện tại xử lý.
```
