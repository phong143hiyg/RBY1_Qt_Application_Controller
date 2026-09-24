# Hai prompt triển khai môi trường test MoveIt 2 cho RBY1

Ngày soạn: 17/09/2026. Đây là đặc tả và prompt để triển khai ở hai repository; chưa phải môi trường ROS 2 đã được build/chạy.

## Cách sử dụng

1. Chép file này sang repository ROS 2, ví dụ `docs/prompts-test-moveit2-rby1.md`, để hai phía có cùng đặc tả.
2. Chạy **Prompt ROS 2** trước để xác nhận model, frame, TCP, scene và kiểm chứng planning.
3. Chạy **Prompt Qt** trong repository hiện tại. Phía Qt có thể làm song song với mock server theo giao thức bên dưới.
4. Kết nối Qt với planning service ROS 2, chạy lại bộ test chung. Chỉ coi tích hợp hoàn tất khi có kết quả chạy thật của hai phía.

## Đặc tả chung bắt buộc cho cả hai prompt

### Phạm vi

- Mốc đầu: RViz + MoveIt 2 + fake hardware, tính quỹ đạo gắp–thả và xem trước. Không điều khiển robot thật.
- ROS 2 sở hữu robot model, Planning Scene, IK, collision checking, task planning và lưu plan. Qt sở hữu nhập liệu, gửi yêu cầu, hiển thị tiến độ và kết quả.
- Fake hardware và attach/detach chỉ kiểm chứng hình học/chuyển động; chưa chứng minh lực gắp, ma sát hoặc vật không trượt/rơi.
- Bắt đầu một tay, base và torso giữ cố định. Chọn `right_arm` nếu model hỗ trợ; không tự mở rộng sang cả hai tay hoặc di chuyển base.
- Ưu tiên baseline Ubuntu 22.04 / ROS 2 Humble theo repository RBY1 chính thức. Nếu workspace dùng distro khác, kiểm tra tương thích và ghi lại lựa chọn trước khi cài/build.
- Xác minh package RBY1 theo model/version; không mặc định robot là M v1.2. Một model minh họa chỉ được dùng trong fake hardware và phải được ghi rõ.
- OMPL cho free-space; Cartesian cho approach/lift/lower/retreat; ưu tiên MoveIt Task Constructor (MTC) để ghép task. Không đưa cả package ROS 2 vào build CMake Qt.

### Scene test

Tạo `config/test_scene.yaml` ở phía ROS 2, có schema version, đơn vị mét/radian, quaternion `[x,y,z,w]`, frame, group, TCP và các thành phần sau:

| Thành phần | ID/vai trò | Yêu cầu |
|---|---|---|
| Sàn | `floor` | Collision geometry; bố trí không tạo collision giả với chân/base |
| Bàn | `table` | Collision box, mặt trên xác định bằng kích thước và pose |
| Vật cản | `obstacle_box`, `obstacle_column` | Box và cylinder giữa vùng gắp/thả; kích thước/pose chỉnh được |
| Vật cần di chuyển | `target_object` | Cylinder hoặc box, kích thước phù hợp độ mở kẹp đã xác minh |
| Pose gắp | `pick_tcp_pose` | Pose của TCP khi gắp; không đồng nhất với tâm vật |
| Pose đặt vật | `place_object_pose` | Pose mong muốn của vật sau khi thả |
| Pose thả | `place_tcp_pose` | Suy ra từ `place_object_pose` và transform TCP–object tại grasp |
| Các pose trung gian | pre-grasp, lift, pre-place, retreat | Hướng và khoảng cách Cartesian cấu hình được, vẽ marker có nhãn |

Không ấn định tọa độ mẫu là reachable/an toàn. Lấy planning frame từ model; kiểm tra TF, joint state, IK và start-state collision rồi hiệu chỉnh scene cho model fake đang dùng. Lưu cấu hình đã hiệu chỉnh và start state làm fixture test; không phụ thuộc kéo thả thủ công trong RViz.

Phân biệt transform rõ ràng: nếu `T_tcp_object` là pose của vật trong TCP tại grasp, thì `T_world_tcp_place = T_world_object_place * inverse(T_tcp_object)`. Nếu TCP có offset so với link cuối, phải tính cả offset đó. Marker gắp/thả không phải collision object.

### Giao thức planning riêng phiên bản 1

Đây là giao thức **mới được đề xuất**, chưa có trong App Bridge hiện tại. Giữ các command điều khiển cũ tương thích. Dùng socket/service planning riêng, port mặc định `8082` cấu hình được; port `8081` hiện tại tiếp tục phục vụ điều khiển cũ. Server mặc định bind localhost; cấu hình địa chỉ truy cập từ Windows trong README.

Transport: UTF-8 NDJSON, một JSON object mỗi dòng. Giới hạn frame 1 MiB; xử lý frame bị chia/gộp qua TCP. Dùng request ID dạng string để tránh mất độ chính xác số trong JSON. ID duy nhất trong phiên; phản hồi/event phải echo đúng ID.

Envelope request:

```json
{"protocol_version":1,"type":"request","request_id":"qt-session-a-1","command":"get_capabilities","payload":{}}
```

Envelope phản hồi nhận yêu cầu:

```json
{"protocol_version":1,"type":"response","request_id":"qt-session-a-1","command":"get_capabilities","ok":true,"payload":{}}
```

Envelope event cho yêu cầu planning dài:

```json
{"protocol_version":1,"type":"event","request_id":"qt-session-a-3","command":"plan_pick_place","event_seq":1,"status":"planning","stage":"approach","payload":{}}
```

- `response.ok=true` là nhận yêu cầu, không phải task hoàn tất. Với lệnh truy vấn ngắn, response chứa ngay kết quả. Với planning, response chỉ ACK, sau đó event kết thúc xác nhận kết quả.
- Status planning: `planning`, `succeeded`, `failed`, `cancelled`. Mỗi yêu cầu planning đã ACK phải có đúng một terminal event. Thành công planning không phải thành công execution.
- Terminal `succeeded` trả `plan_id`, `scene_revision`, `robot_model_id`, group, frame, TCP, thời gian planning, duration, số waypoint, tên joint, danh sách stage và `validation` (joint limits, collision, timing). `robot_model_id` phải gắn với model/version và dấu vết URDF/SRDF/config, không chỉ là tên hiển thị.
- Lỗi dùng `ok=false` hoặc terminal `failed`, kèm `error: {code,message,stage,details}` ở cấp envelope. Code tối thiểu: `INVALID_REQUEST`, `UNSUPPORTED_COMMAND`, `BUSY`, `TF_UNAVAILABLE`, `STATE_UNAVAILABLE`, `START_IN_COLLISION`, `GOAL_IN_COLLISION`, `NO_IK`, `PLANNING_FAILED`, `CARTESIAN_INCOMPLETE`, `VALIDATION_FAILED`, `TIMEOUT`, `STALE_PLAN`. Không báo `NO_IK` nếu backend không đủ bằng chứng phân biệt nguyên nhân.
- `get_capabilities`: trả `backend_mode`, model/version, `robot_model_id`, planning frame, groups, TCP/link mappings, commands hỗ trợ, `execution_enabled=false`, giới hạn payload và timeout đề xuất. Qt chỉ bật chức năng tương ứng capability được xác nhận.
- `load_test_scene`: payload `{ "scenario_id": "pick_place_obstacles" }`; ROS 2 nạp fixture và trả scene snapshot + revision. Các fixture khác: `baseline`, `goal_in_collision`, `unreachable_goal`, `start_in_collision`, `attached_object_clearance`. Client không gửi đường dẫn file tùy ý.
- `get_scene`: trả scene hiện tại, object IDs, geometry/poses, markers/poses và revision. Mỗi pose dùng `{frame_id,position:[x,y,z],orientation_xyzw:[x,y,z,w]}`. Không gửi TCP pose không có frame.
- `plan_to_pose`: payload gồm `scene_revision`, `group`, `tcp_frame`, `goal_tcp_pose`, `planning_timeout_s`, `velocity_scale`, `acceleration_scale`.
- `plan_pick_place`: payload gồm `scene_revision`, `group`, `tcp_frame`, `object_id`, `pick_tcp_pose`, `place_object_pose`, `approach_distance_m`, `lift_distance_m`, `retreat_distance_m`, `planning_timeout_s`, `velocity_scale`, `acceleration_scale`. Hướng approach/lift/lower/retreat lấy từ scene snapshot; Qt phải hiển thị và dùng đúng cấu hình đó.
- `preview_plan`: payload `{ "plan_id": "..." }`; hiển thị solution trong RViz và trả metadata. Không chuyển thành lệnh execution, không stream hàng trăm waypoint để Qt điều khiển robot.
- `get_task_status`: payload `{ "target_request_id": "..." }`; trả snapshot gồm trạng thái/stage/sequence mới nhất và kết quả cuối nếu có. Dùng đối chiếu sau mất event hoặc reconnect.
- `cancel_planning`: payload `{ "target_request_id": "..." }`; có request ID riêng cho command cancel. ACK cancel không kết thúc task gốc; terminal event của task gốc mới xác nhận kết thúc. Nếu planner không hỗ trợ ngắt tức thì, giữ trạng thái đang hủy đến khi worker dừng, không công bố/lưu plan từ kết quả đến muộn.
- Mốc này không cung cấp `execute_plan`. Preview làm việc trên snapshot; scene live không bị thay đổi theo trạng thái giả cuối trajectory. Revision/model thay đổi phải vô hiệu hóa plan cũ. Worker planning dùng snapshot nhất quán, không đọc scene đang thay đổi giữa chừng.
- Chỉ một planning task hoạt động; task tiếp theo trả `BUSY`. `load_test_scene` cũng trả `BUSY` khi đang planning. Các truy vấn/status/cancel vẫn được xử lý, không chặn event loop.
- `plan_id` được lưu phía ROS 2 với TTL, start state, model ID và scene revision. Công bố TTL qua capability. Worker có deadline hữu hạn; giữ status/kết quả trong TTL để truy vấn sau reconnect. Sau reconnect Qt không gửi lại planning tự động.

## Prompt Qt — dùng trong repository Windows hiện tại

```text
Hãy triển khai giao diện test tính quỹ đạo MoveIt 2 cho RBY1 trong repository Qt C++17 này, theo toàn bộ “Đặc tả chung” của file prompts-test-moveit2-rby1.md được cung cấp trong context. Đây là yêu cầu thực hiện code và kiểm tra, không chỉ viết kế hoạch.

Trước khi sửa, đọc AGENTS.md áp dụng, README.md, CMakeLists.txt, question/ke-hoach-moveit2-rby1.md, src/network/{RobotClient,NdjsonParser}.*, src/controller/RobotController.*, src/state/* và tests/TestSystemStatus.cpp. Giữ thay đổi sẵn có của người dùng.

RobotClient hiện tạo request ID local và ghép một số response theo hàng đợi; không dùng nguyên cơ chế đó để nhận event planning bất đồng bộ. Thêm PlanningClient dùng QTcpSocket riêng với protocol_version=1, wire request_id dạng string, phân biệt response/event và tra cứu theo ID. Tái sử dụng parser NDJSON nếu phù hợp. Không sửa giao thức command robot cũ để giả định Bridge đã hỗ trợ MoveIt.

Thêm tab/panel “Test quỹ đạo”: host/port planning; capability/model/frame/TCP/backend mode; chọn scenario; xem bảng vật cản/vật gắp; nhập pick TCP pose và place object pose; các tham số approach/lift/retreat và scaling; nút Nạp scene, Plan pose, Plan gắp–thả, Xem trước RViz, Hủy planning. Các giá trị ban đầu lấy từ scene ROS hoặc mock, không hard-code tọa độ là hợp lệ. Hiển thị đơn vị và quaternion rõ ràng; từ chối NaN/Inf, quaternion không hợp lệ, scaling ngoài (0,1], khoảng cách âm và timeout sai. Hiển thị progress/stage, lỗi, plan_id, scene revision, duration, số waypoint và kết quả validation.

Planning có trạng thái riêng Idle/Planning/PlanReady/Failed/Cancelling/Cancelled/Disconnected hoặc tương đương. Planning offline không phụ thuộc Power/Servo/Stream của robot thật. Không đưa planning event vào RobotState::onResponse như một ACK joint. Không coi ACK thành công là có plan; không coi timeout/disconnect là cancel đã thành công. Bỏ event cũ/sai ID, chống cập nhật lùi bằng event_seq; reconnect đối chiếu get_task_status trước khi bật thao tác mới. Scene/model thay đổi làm plan mất hiệu lực. Panel không có nút Execute ở mốc này.

Thêm mock planning server chạy độc lập bằng Python standard library để test trên Windows khi chưa có ROS 2. Mock theo đúng giao thức chung và có chế độ ACK trước terminal event, fragment/coalesce frame, progress, failure, delay/timeout, cancellation, disconnect/reconnect và stale revision. Hiển thị MOCK rõ ràng; mock không được báo rằng đã tính IK hoặc kiểm tra collision thật. Dùng fixture contract chung, mock validation phải đánh dấu simulated, không giả kết quả MoveIt.

Viết test cần thiết cho correlation theo request ID, out-of-order event, terminal trước ACK, timeout/late response, cancel race, reconnect, NDJSON và invalid input. Chạy build/test Qt hiện có và test mới bằng toolchain phù hợp README. Ghi rõ kiểm tra nào chưa chạy và lý do; không tự nhận đã kết nối ROS 2 nếu chỉ chạy mock.

Cập nhật README hướng dẫn chạy mock, dùng tab và kết nối ROS 2, tạo tài liệu protocol v1 cùng JSON fixture dùng chung. Hoàn thành phần có thể làm trong repo này; báo file đã sửa, kết quả build/test và phần còn cần ROS 2 xác minh. Không nhúng MoveIt vào Qt, không gửi waypoint đến driver, không gọi robot thật.
```

## Prompt ROS 2 — dùng trong repository/workspace Ubuntu

```text
Hãy triển khai môi trường test tính quỹ đạo MoveIt 2 cho RBY1 theo toàn bộ “Đặc tả chung” của file prompts-test-moveit2-rby1.md được cung cấp trong context. Cần tạo package, launch, scene và test chạy được; không dừng ở tài liệu kế hoạch. Mốc này chỉ planning/preview, không điều khiển robot thật.

Đọc AGENTS.md áp dụng và khảo sát workspace: ROS_DISTRO, MoveIt, RBY1 SDK, rby1-ros2 commit, URDF/SRDF, group/IK solver, joint limits, TCP/gripper links, controller configs và launch arguments. Ghi version/commit trong báo cáo. Không tự chọn model của robot thật. Nếu chưa biết model, dùng model minh họa có sẵn cho fake hardware và ghi rõ; để model/package là launch argument bắt buộc. Giữ code Bridge cũ và thay đổi người dùng.

Tạo package ament_cmake C++17 rby1_motion_planning với package.xml/CMakeLists, config/test_scene.yaml, các fixture scenario, launch/planning_test.launch.py, node nạp scene, planning/task worker, validator và planning NDJSON service riêng. Launch một lệnh dựng robot_state_publisher, MoveIt move_group, joint state/fake hardware, RViz, scene loader và planning service; tái sử dụng launch/config RBY1 đúng package. Truyền use_fake_hardware=true rõ ràng và kiểm tra đã dùng mock_components/GenericSystem; không kế thừa default false của demo chính thức. Tránh chạy nhiều publisher/controller cùng sở hữu joint state.

Lấy planning frame từ model và xác minh TCP, end-effector semantics, gripper group, touch_links, trạng thái mở/đóng và giới hạn gripper. Không sao chép tên panda/open/close từ tutorial. Nếu SRDF thiếu end_effector, tạo overlay config với diff được ghi nhận, không sửa âm thầm package vendor. Nếu chưa đủ cấu hình để tính grasp hợp lệ, báo capability/lỗi rõ ràng; hoàn thành scene và plan_to_pose thay vì giả task thành công.

Dựng scene bằng CollisionObject từ YAML gồm sàn, bàn, box/cylinder obstacle và target_object. Chờ và xác nhận scene được áp dụng; lấy joint state mới với timeout; kiểm tra TF/IK/start collision; hiệu chỉnh fixture cho model fake đang dùng. Publish marker có nhãn cho pick TCP, place object, place TCP, pre-grasp, lift, pre-place và retreat. Cấu hình các khoảng cách/hướng, pose, kích thước và scaling qua YAML/parameters. Scene reset phải đưa về trạng thái sạch, không tích lũy object/ACM/attachment giữa các test.

Triển khai plan_to_pose bằng MoveGroupInterface hoặc planning pipeline tương đương. Triển khai plan_pick_place bằng MTC: current state, open gripper, connect pre-grasp, Cartesian approach, cho phép contact chỉ giữa target và các link ngón kẹp đã xác minh, close gripper, attach, lift, transfer tránh vật cản, lower, open gripper, detach tại place object pose, retreat. Xử lý contact với support surface đúng stage và khôi phục ACM sau khi rời tiếp xúc. Không disable collision của toàn bộ tay với bàn/vật cản.

Mỗi stage phải tiếp tục từ final robot state và scene diff của stage trước. Planning-only dùng scene snapshot/diff của task; không attach vật vào scene live khi robot live vẫn ở start pose, không dùng lại getCurrentState cho mọi stage. Tính pose thả theo transform TCP–object và TCP–link; check collision của cả vật attached. Cartesian không đủ fraction phải thất bại, không nhận phần quỹ đạo ngắn là task hoàn tất. Không gọi execute() trong đường xử lý này.

Kiểm tra quỹ đạo từng stage và nối stage: joint bounds, self/world collision và attached-object collision theo đúng scene/ACM của stage, liên tục joint, timestamps tăng trong stage, vận tốc/gia tốc theo giới hạn. Kiểm tra cả đoạn nội suy giữa waypoint với độ phân giải cấu hình và công bố độ phân giải; không khẳng định collision-free liên tục chỉ từ sample. Retiming khi cần bằng thuật toán có trong MoveIt đã cài, không tự điền timestamps tùy ý. Stage có attach/detach/gripper event phải giữ metadata, không ghép tất cả thành một JointTrajectory bỏ mất scene diff. Publish preview qua DisplayTrajectory và MTC introspection nếu có để xem object đi cùng tay.

Triển khai planning service đúng protocol v1 và fixture JSON chung: capabilities, load/get scene, plan pose/pick-place, preview, get task status, cancel. Planning chạy worker riêng; ACK/event/status có request ID; deadline hữu hạn, một task active và một terminal event. Lưu plan phía ROS, TTL/model ID/revision/start state; không nhận plan của scene cũ, không tự execute. Nếu không ngắt được planner ngay, hủy publication kết quả muộn và chỉ xác nhận cancelled khi worker đã dừng. Backend phải công bố execution_enabled=false.

Viết test thực bằng ament/launch_testing hoặc công cụ phù hợp: baseline reachable; obstacle phải thực sự chặn đường thẳng nhưng có đường vòng và test dương phải plan thành công sau hiệu chỉnh; goal collision/unreachable; start collision; joint limit; frame sai/TF thiếu; attached object chặn lối mà bare gripper đi được; object pose sau detach trong solution; không thay đổi scene live sau plan/preview; timeout/cancel; reset/repeat; scene revision/plan invalidation; protocol correlation. Test âm được lỗi rõ ràng, không trajectory giả. Không yêu cầu waypoint giống hệt qua các lần chạy OMPL; lưu seed nếu backend hỗ trợ, version/start state/scene và báo tỷ lệ thành công/thời gian qua số lần lặp cấu hình được.

Chạy colcon build/test, launch fake hardware, chạy các scenario và test NDJSON từ CLI. Lưu báo cáo JSON/CSV, scene/start/goal, stage errors, planning time/duration/waypoints, validation và ảnh RViz nếu môi trường có GUI. README phải có lệnh cài dependency, build, launch một lệnh, test, preview, reset và cách Qt Windows kết nối service. Đưa câu lệnh thật theo package đã xác minh, không placeholder khó chạy. Nếu thiếu dependency/GUI thì hoàn thành code còn lại và ghi kiểm tra chưa chạy; không báo PASS khi chưa kiểm chứng. Không triển khai physics simulator hoặc robot thật trong mốc này.
```

## Tiêu chí nghiệm thu hai phía

- ROS 2: một launch command dựng scene; task gắp–thả có vật cản plan thành công trên fixture đã hiệu chỉnh, preview được, validation và test âm có kết quả thực.
- Qt: chạy được với mock; tiến độ/event ghép đúng request; kết nối backend ROS 2 và xem kết quả cùng revision/model/frame.
- Plan/preview không phát lệnh chuyển động lên robot; scene live không bị biến thành trạng thái cuối giả của task.
- Bộ fixture giao thức dùng chung; khác biệt triển khai phải sửa thống nhất hai phía, không tự thay đổi schema một bên.
- Có báo cáo phân biệt test mock, planning thật trên fake hardware và test chưa chạy. Fake hardware không được gọi là kiểm chứng grasp vật lý.

## Nguồn kỹ thuật đã đối chiếu

- [RBY1 MoveIt2 chính thức](https://rainbowrobotics.github.io/rby1-dev/ros2/ros2_driver/rby1_moveit2.html): package theo model/version, fake hardware và default `use_fake_hardware=false`.
- [Repository rby1-ros2](https://github.com/RainbowRobotics/rby1-ros2): baseline ROS 2 Humble, cấu hình và lệnh launch.
- [MoveIt Task Constructor pick-and-place — Humble](https://moveit.picknik.ai/humble/doc/tutorials/pick_and_place_with_moveit_task_constructor/pick_and_place_with_moveit_task_constructor.html): task/stage và scene changes cho gắp–thả; các tên group của Panda trong ví dụ phải thay bằng model RBY1 đã kiểm chứng.
