# Kế hoạch sử dụng MoveIt 2 để tính quỹ đạo cho RBY1

Hai prompt triển khai cho repository Qt và workspace ROS 2, cùng đặc tả scene test và giao thức planning: [prompts-test-moveit2-rby1.md](prompts-test-moveit2-rby1.md). Tài liệu này là đầu vào triển khai; chưa xác nhận môi trường ROS 2 đã chạy thành công.

> Cập nhật: 11/09/2026  
> Phạm vi hiện tại: hoàn thành lựa chọn framework và thiết kế môi trường test. Chỉ triển khai các bước tiếp theo sau khi hai bước này đạt tiêu chí nghiệm thu.

## 1. Mục tiêu

Xây dựng chức năng để RBY1 có thể:

- nhận điểm đích hoặc điểm gắp/thả;
- tính quỹ đạo thỏa giới hạn khớp;
- tránh tự va chạm và vật cản trong môi trường;
- kiểm tra quỹ đạo trong mô phỏng trước khi chạy robot thật;
- sau này nhận yêu cầu từ ứng dụng Qt thông qua App Bridge.

Kiến trúc mục tiêu:

```text
Qt Desktop
    |
    | TCP/NDJSON: mục tiêu tác vụ, không phải từng điểm joint
    v
App Bridge / ROS 2 Planning Node
    |
    +--> MoveIt 2 move_group
    |       +--> Planning Scene
    |       +--> OMPL / Cartesian planner
    |       +--> MoveIt Task Constructor (giai đoạn pick-and-place)
    |
    +--> ros2_control
            +--> fake hardware
            +--> RBY1 simulator
            +--> RBY1SystemHardware trên robot thật
```

MoveIt 2 và code tính quỹ đạo phải chạy trong môi trường ROS 2, tức phía App Bridge/planning service trên Ubuntu. Ứng dụng Qt trên Windows chỉ gửi mục tiêu, nhận tiến độ và hiển thị kết quả; không nhúng MoveIt trực tiếp vào `RobotController`.

---

## 2. Bước 1 — Lựa chọn framework tính toán quỹ đạo

### 2.1. Lựa chọn

Chọn **MoveIt 2** làm framework motion planning chính, sử dụng cấu hình RBY1 chính thức tương ứng đúng model và phiên bản robot.

Các lớp công nghệ được chọn:

| Vai trò | Công nghệ | Cách sử dụng |
|---|---|---|
| Framework planning | MoveIt 2 | Quản lý robot model, IK, joint limits, collision checking, planning và execution |
| Planner ban đầu | OMPL, ưu tiên `RRTConnectkConfigDefault` | Lập quỹ đạo free-space từ trạng thái hiện tại tới pose đích và tránh vật cản |
| Đoạn chuyển động thẳng | Cartesian path hoặc Pilz `LIN` sau khi xác minh pipeline | Approach, lift, hạ vật và retreat |
| Điều phối gắp–thả | MoveIt Task Constructor (MTC), triển khai sau Bước 2 | Ghép nhiều stage: mở kẹp, tiếp cận, gắp, nâng, di chuyển, thả và rút tay |
| Giao tiếp robot | `ros2_control` + `rby1_hardware/RBY1SystemHardware` | Chuyển trajectory của MoveIt xuống RBY1 SDK/robot |
| Giao diện C++ | `MoveGroupInterface` và `PlanningSceneInterface` | Phù hợp với App Bridge C++ và dễ kiểm soát plan/execute riêng biệt |

MoveIt 2 là framework; OMPL là backend tìm đường; MTC là lớp xây dựng tác vụ nhiều bước. Gazebo, MuJoCo và Isaac Sim là simulator, không thay thế motion planner.

### 2.2. Lý do chọn MoveIt 2

- Tài liệu RBY1 xác nhận `rby1_hardware` kết nối RBY1 SDK với MoveIt 2 qua pipeline chuẩn `ros2_control`.
- Các package `rby1_moveit_*` đã chứa SRDF, kinematics, joint limits và controller configuration theo từng model/version.
- Cấu hình chính thức đã có các planning group như `right_arm`, `left_arm`, `both_arms`, `torso`, `head`, `body`, `gripper_r` và `gripper_l`.
- `PlanningSceneInterface` hỗ trợ thêm/xóa vật cản và attach/detach vật đang được gắp.
- MTC phù hợp với pick-and-place vì chia một tác vụ phức tạp thành các stage độc lập nhưng có liên hệ trạng thái.

Nguồn chính:

- [RBY1 MoveIt2 — Rainbow Robotics](https://rainbowrobotics.github.io/rby1-dev/ros2/ros2_driver/rby1_moveit2.html)
- [RBY1 ROS 2 repository — Rainbow Robotics](https://github.com/RainbowRobotics/rby1-ros2)
- [Move Group C++ Interface — MoveIt](https://moveit.picknik.ai/main/doc/examples/move_group_interface/move_group_interface_tutorial.html)
- [Motion Planning — MoveIt](https://moveit.picknik.ai/main/doc/concepts/motion_planning.html)

### 2.3. Phiên bản môi trường được đề xuất

Chọn baseline ít rủi ro nhất theo tài liệu hiện tại:

```text
OS:             Ubuntu 22.04
ROS 2:          Humble
MoveIt:         ros-humble-moveit
RBY1 ROS 2:     nhánh/package tương thích Humble
RBY1 SDK:       0.10.x hoặc phiên bản được package driver yêu cầu
Ngôn ngữ node:  C++17/rclcpp
```

Không dùng tên package mẫu `rby1_moveit_m_1_2` cho tới khi xác định chính xác robot. Phải chọn một package trong bảng chính thức:

```text
rby1_moveit_a_1_0
rby1_moveit_a_1_1
rby1_moveit_a_1_2
rby1_moveit_m_1_0
rby1_moveit_m_1_1
rby1_moveit_m_1_2
rby1_moveit_m_1_3
```

Sai model/version có thể làm MoveIt và robot sử dụng cấu hình joint/kinematics khác nhau. Tài liệu RBY1 cảnh báo plugin không tự phát hiện được trường hợp lệch phiên bản này.

Quy ước trong tài liệu:

```bash
export RBY1_MOVEIT_PKG=<package_dung_model_va_version>
```

Ví dụ minh họa cho Model M v1.2 là `rby1_moveit_m_1_2`, nhưng không được sao chép giá trị này sang robot thật nếu chưa kiểm tra model.

### 2.4. Cách tích hợp với project Qt hiện tại

Không để Qt gửi trực tiếp một mảng hàng trăm joint waypoint. App Bridge nên sở hữu toàn bộ vòng đời trajectory:

```text
1. Qt gửi pose/task goal và request_id.
2. App Bridge cập nhật Planning Scene.
3. MoveIt lập kế hoạch nhưng chưa execute.
4. App Bridge trả về plan result, lỗi cụ thể và metadata.
5. Chỉ execute khi plan hợp lệ và robot vẫn ở trạng thái cho phép.
6. App Bridge phát trạng thái Planning/Executing/Succeeded/Failed/Cancelled.
7. Qt hiển thị canonical status từ Bridge.
```

Việc thiết kế command NDJSON mới như `plan_pose`, `execute_plan` hoặc `pick_place` thuộc giai đoạn sau. Không thay đổi schema App Bridge hiện tại trong Bước 1 và Bước 2.

### 2.5. Tiêu chí hoàn thành Bước 1

- [ ] Xác nhận RBY1-A hay RBY1-M và đúng version phần cứng.
- [ ] Ghi lại version ROS 2, MoveIt 2, `rby1-ros2` và RBY1 SDK.
- [ ] Launch được package `rby1_moveit_*` đúng phiên bản với fake hardware.
- [ ] RViz hiển thị đúng model, TF và trạng thái joint.
- [ ] Liệt kê đúng planning group và controller đang active.
- [ ] Node C++ dùng `MoveGroupInterface` lập được một trajectory tới pose an toàn.
- [ ] Tách riêng `plan()` và `execute()`; test đầu tiên chỉ plan, chưa điều khiển robot thật.

Chỉ chuyển sang Bước 2 khi toàn bộ mục trên đạt.

---

## 3. Bước 2 — Tạo môi trường test

### 3.1. Chiến lược hai tầng

#### Tầng A — Planning test bằng RViz + fake hardware

Đây là môi trường phải làm trước:

```bash
source /opt/ros/humble/setup.bash
source ~/rby1_ros2_ws/install/setup.bash
ros2 launch "$RBY1_MOVEIT_PKG" demo.launch.py use_fake_hardware:=true
```

`use_fake_hardware:=true` sử dụng `mock_components/GenericSystem`, không cần robot thật. Môi trường này dùng để kiểm tra:

- IK và khả năng đạt pose;
- joint limits;
- self-collision;
- tránh vật cản;
- attach/detach vật gắp trong Planning Scene;
- tính hợp lệ của toàn bộ trajectory.

Fake hardware không mô phỏng lực, ma sát, vật rơi hay tiếp xúc thật giữa ngón kẹp và đồ vật. Thao tác gắp ở tầng này là attach object vào end-effector theo logic của MoveIt.

#### Tầng B — Physics test bằng simulator

Sau khi Tầng A ổn định, dùng simulator chính thức của RBY1:

- ưu tiên MuJoCo Docker `rainbowroboticsofficial/rby1-sim` để kiểm tra interface RBY1 SDK/driver;
- có thể đánh giá Isaac Sim nếu cần camera, gripper physics hoặc môi trường USD phức tạp;
- chỉ dùng Gazebo nếu đội dự án chấp nhận tự xây dựng/kiểm chứng RBY1 `ros2_control` và model simulation, vì tài liệu RBY1 được dẫn không cung cấp quy trình Gazebo hoàn chỉnh cho MoveIt.

Nguồn:

- [RBY1 MuJoCo simulator](https://rainbowrobotics.github.io/rby1-dev/simulators/mujoco.html)
- [RBY1 Isaac Sim](https://rainbowrobotics.github.io/rby1-dev/simulators/isaac_sim.html)
- [Planning Scene ROS API — MoveIt](https://moveit.picknik.ai/main/doc/examples/planning_scene_ros_api/planning_scene_ros_api_tutorial.html)

Tầng B chưa được xem là sẵn sàng cho pick-and-place vật lý cho tới khi xác minh được:

- có thể chỉnh world/MJCF/USD để thêm bàn, vật cản và vật gắp;
- trạng thái gripper được mô phỏng;
- object contact/friction hoạt động;
- trạng thái simulator và MoveIt Planning Scene được đồng bộ.

### 3.2. Cài đặt workspace thử nghiệm

Các lệnh baseline:

```bash
sudo apt update
sudo apt install ros-humble-moveit \
  ros-humble-moveit-visual-tools \
  ros-humble-interactive-markers \
  ros-humble-gripper-controllers \
  ros-humble-joint-trajectory-controller

mkdir -p ~/rby1_ros2_ws/src
cd ~/rby1_ros2_ws/src
git clone https://github.com/RainbowRobotics/rby1-ros2.git

cd ~/rby1_ros2_ws
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install
source install/setup.bash
```

Kiểm tra trước khi viết node:

```bash
ros2 pkg list | grep rby1_moveit
ros2 node list | grep move_group
ros2 control list_controllers
ros2 topic echo /joint_states --once
ros2 action list | grep follow_joint_trajectory
```

Kết quả mong đợi là `move_group`, `robot_state_publisher`, controller manager và controller tương ứng planning group đều hoạt động.

### 3.3. Package test riêng

Tạo package ROS 2 riêng, không đặt code MoveIt trong project Qt:

```bash
cd ~/rby1_ros2_ws/src
ros2 pkg create rby1_motion_planning \
  --build-type ament_cmake \
  --dependencies rclcpp moveit_ros_planning_interface \
  moveit_msgs geometry_msgs shape_msgs tf2_geometry_msgs
```

Cấu trúc đề xuất:

```text
rby1_motion_planning/
├── CMakeLists.txt
├── package.xml
├── config/
│   └── test_scene.yaml
├── launch/
│   └── scene_test.launch.py
├── src/
│   ├── scene_loader.cpp
│   └── plan_to_pose.cpp
└── test/
    └── test_scene_planning.cpp
```

### 3.4. Thiết kế scene tối thiểu

Scene đầu tiên nên đơn giản và tái lập được:

| Thành phần | MoveIt representation | Mục đích |
|---|---|---|
| Sàn | Box collision object | Ngăn quỹ đạo đi xuyên xuống sàn |
| Bàn | Box collision object | Mặt đặt vật và vùng va chạm chính |
| Vật cản | Box/cylinder collision object | Buộc planner tìm đường vòng |
| Vật cần gắp | Cylinder hoặc box có ID `target_object` | Attach khi gắp, detach khi thả |
| Điểm gắp | `PoseStamped` + marker/TF `pick_pose` | Pose mục tiêu của end-effector |
| Điểm thả | `PoseStamped` + marker/TF `place_pose` | Pose đặt vật |

Không hard-code frame là `world` nếu chưa xác minh. Node phải lấy planning frame từ:

```cpp
const std::string planningFrame = moveGroup.getPlanningFrame();
```

Mọi collision object và pose cần dùng cùng frame hoặc có TF hợp lệ. Tọa độ dưới đây chỉ là cấu trúc dữ liệu minh họa, không phải tọa độ an toàn cho robot thật:

```yaml
planning_group: right_arm
end_effector_link: ee_right

objects:
  - id: table
    shape: box
    size: [1.20, 0.80, 0.08]
    pose: [0.70, 0.00, 0.70, 0.0, 0.0, 0.0, 1.0]

  - id: obstacle_wall
    shape: box
    size: [0.10, 0.50, 0.50]
    pose: [0.55, 0.00, 1.00, 0.0, 0.0, 0.0, 1.0]

  - id: target_object
    shape: cylinder
    size: [0.20, 0.04]
    pose: [0.65, -0.20, 0.84, 0.0, 0.0, 0.0, 1.0]

pick_pose:  [0.65, -0.20, 0.96, 0.0, 1.0, 0.0, 0.0]
place_pose: [0.65,  0.25, 0.96, 0.0, 1.0, 0.0, 0.0]
```

Giá trị thực tế phải được hiệu chỉnh trong RViz theo workspace của đúng model RBY1.

### 3.5. Trình tự dựng scene

1. Khởi tạo `MoveGroupInterface` cho `right_arm` hoặc `left_arm`.
2. Đọc `planning_frame`, end-effector link và current state.
3. Tạo sàn, bàn, vật cản và `target_object` bằng `moveit_msgs::msg::CollisionObject`.
4. Gọi `PlanningSceneInterface::applyCollisionObjects()`.
5. Chờ và xác nhận các ID đã xuất hiện trong Planning Scene.
6. Kiểm tra start state không collision.
7. Lập kế hoạch tới pre-grasp pose khi chưa có vật cản để tạo baseline.
8. Thêm vật cản và lập kế hoạch lại; trajectory phải đổi hoặc planning phải thất bại an toàn.
9. Attach `target_object` vào `ee_right`/`ee_left`, khai báo đúng `touch_links` của gripper.
10. Lập kế hoạch mang vật tới `place_pose`; collision geometry của vật phải đi cùng robot.
11. Detach vật tại điểm thả và cập nhật lại pose của object trong world.

MoveIt cung cấp chính xác cơ chế add/remove collision object và attach/detach object này trong `PlanningSceneInterface`.

### 3.6. Điểm cần xác minh trong cấu hình RBY1

Package RBY1-M v1.2 hiện có group `right_arm`, `left_arm`, `gripper_r`, `gripper_l` và link `ee_right`, `ee_left`. Tuy nhiên, file SRDF đang được tham khảo không thể hiện rõ khai báo semantic `<end_effector>`.

Trước khi triển khai MTC phải kiểm tra:

```cpp
moveGroup.getEndEffectorLink();
robotModel->getEndEffectors();
```

Nếu không có end-effector semantic, cần cấu hình rõ `eef`, `ik_frame`, parent link, gripper group và `touch_links` trong package MoveIt tùy chỉnh. Không tự sửa package chính thức trước khi ghi nhận diff và kiểm tra trên fake hardware.

### 3.7. Bộ test tối thiểu

| Test | Kết quả mong đợi |
|---|---|
| Plan tới pose hợp lệ, chưa có vật cản | Thành công, trajectory không rỗng |
| Thêm vật cản chắn đường trực tiếp | Planner đi vòng hoặc trả lỗi planning, không xuyên vật cản |
| Goal nằm bên trong vật cản | Planning thất bại |
| Goal ngoài workspace | IK/planning thất bại có mã lỗi |
| Start state collision | Không execute |
| Attach `target_object` | Object biến mất khỏi world và xuất hiện trong attached objects |
| Mang object qua gần vật cản | Collision checking tính cả kích thước vật đang mang |
| Detach tại `place_pose` | Object trở lại world với pose mới |
| Vượt joint limits | Bị từ chối |
| Chạy lặp cùng scene/config | Kết quả tái lập được trong giới hạn thời gian planning |

Lưu cho mỗi lần test:

- start state và goal;
- scene object IDs và poses;
- planning group, pipeline và planner ID;
- planning time, số lần thử và mã lỗi;
- trajectory duration và số waypoint;
- kết quả collision validation;
- ảnh hoặc rosbag phục vụ tái hiện lỗi.

### 3.8. Tiêu chí hoàn thành Bước 2

- [ ] Một launch command dựng được toàn bộ môi trường test.
- [ ] RViz hiển thị đúng sàn, bàn, vật cản, vật gắp, pick pose và place pose.
- [ ] Scene được tạo từ file cấu hình, không phụ thuộc thao tác kéo thả thủ công.
- [ ] Planner tránh được vật cản trong test dương.
- [ ] Planner từ chối goal collision/unreachable trong test âm.
- [ ] Attach/detach object hoạt động đúng trong Planning Scene.
- [ ] Test chạy nhiều lần mà không gửi lệnh tới robot thật.
- [ ] Ghi rõ giới hạn: đây là planning simulation, chưa chứng minh lực gắp và contact physics.

Chỉ lập trình pick-and-place hoàn chỉnh khi toàn bộ mục trên đạt.

---

## 4. Kế hoạch tiếp theo sau khi hoàn thành Bước 1 và Bước 2

### Giai đoạn 3 — Pick-and-place bằng MoveIt Task Constructor

Xây pipeline:

```text
CurrentState
 -> Open gripper
 -> Connect to pre-grasp
 -> GenerateGraspPose + ComputeIK
 -> Cartesian approach
 -> Allow hand/object collision
 -> Close gripper
 -> Attach object
 -> Cartesian lift
 -> Connect to place
 -> GeneratePlacePose + ComputeIK
 -> Cartesian lower
 -> Open gripper
 -> Detach object
 -> Retreat
 -> Return safe pose
```

MTC là lựa chọn chính thức được MoveIt khuyến nghị cho pick-and-place nhiều bước:

- [MoveIt Task Constructor concepts](https://moveit.picknik.ai/main/doc/concepts/moveit_task_constructor/moveit_task_constructor.html)
- [Pick and Place with MoveIt Task Constructor](https://moveit.picknik.ai/main/doc/tutorials/pick_and_place_with_moveit_task_constructor/pick_and_place_with_moveit_task_constructor.html)

### Giai đoạn 4 — Simulator vật lý

- kết nối trajectory execution với RBY1 simulator;
- thêm bàn, object và obstacle vào world vật lý;
- đồng bộ object pose từ simulator sang Planning Scene;
- kiểm tra gripper contact, friction, object slip và failed grasp;
- thêm cơ chế replan nếu trạng thái thực khác kế hoạch.

### Giai đoạn 5 — API của App Bridge

Thiết kế schema có version và `request_id`, tối thiểu gồm:

```text
set_scene
plan_to_pose
plan_pick_place
execute_plan
cancel_trajectory
trajectory_status
```

Phân biệt rõ `plan accepted`, `plan succeeded`, `execution started`, `execution succeeded` và `execution failed`. Command ACK không được dùng làm trạng thái hoàn tất.

### Giai đoạn 6 — Tích hợp Qt Desktop

- thêm UI nhập/chọn pick pose và place pose;
- hiển thị Planning/Executing/Failed/Succeeded;
- hiển thị lỗi IK, collision, timeout và controller rejection;
- thêm state mới như `PlanningState` và `ExecutingTrajectoryState`, thay vì dùng chung mọi thứ trong `JointBusyState`;
- hỗ trợ Cancel nhưng không optimistic-update trạng thái robot.

### Giai đoạn 7 — Chạy robot thật

Chỉ chuyển sang robot thật khi:

- model/version khớp tuyệt đối;
- trajectory đã qua fake hardware và simulator vật lý;
- giới hạn velocity/acceleration được giảm cho lần chạy đầu;
- có vùng làm việc trống, E-Stop trong tầm tay và người giám sát;
- Planning Scene phản ánh đúng môi trường thật.

RBY1 không tự cung cấp phát hiện mọi vật cản bên ngoài, vì vậy không được xem collision checking nội bộ là lớp an toàn duy nhất. Tham khảo [RBY1 General Precautions](https://rainbowrobotics.github.io/rby1-dev/precautions/general.html).

---

## 5. Kết luận

Quyết định hiện tại là:

```text
MoveIt 2 + cấu hình RBY1 chính thức
    + OMPL/RRTConnect cho chuyển động free-space
    + Cartesian/Pilz cho approach, lift và retreat
    + PlanningSceneInterface cho vật cản và vật gắp
    + MoveIt Task Constructor cho pick-and-place ở giai đoạn kế tiếp
```

Mốc thực hiện gần nhất không phải tích hợp ngay vào Qt. Trước tiên cần xác nhận đúng model/version RBY1, launch fake hardware thành công, sau đó xây một scene test tái lập được có bàn, vật cản, object, pick pose và place pose. Khi hai mốc này đạt tiêu chí nghiệm thu mới tiếp tục MTC, simulator vật lý và App Bridge.
