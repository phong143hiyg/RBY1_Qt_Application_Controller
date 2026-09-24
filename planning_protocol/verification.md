# Kiểm chứng triển khai Qt planning v1

Ngày: 17/09/2026. Phạm vi kết quả dưới đây: **Qt + mock protocol**, không phải kết quả MoveIt/ROS 2.

## Kết quả đã chạy

Toolchain Windows: Qt 6.11.1 `mingw_64`, GCC/MinGW 13.1.0, Ninja, C++17 Release; Python 3.14.3 standard library.

| Kiểm tra | Kết quả |
|---|---|
| Configure qua qt-cmake | PASS |
| Build app, robot tests, planning tests | PASS |
| Deploy runtime app bằng windeployqt | PASS |
| `RBY1DesktopQtTests` | PASS; 21 JUnit cases kể cả init/cleanup; không sửa test robot cũ |
| `RBY1PlanningTests` | PASS; 21 JUnit cases kể cả init/cleanup và 7 integration rows |
| `PlanningMockTests` | PASS; 6 Python tests trên TCP thực localhost |
| CTest bản cuối | **3/3 suites PASS**, 0 failures, 86.46 s |
| Kiểm tra ảnh panel 1200×840 | PASS; đã mở ảnh, kiểm tra pose/units/xyzw/MOCK và action/progress cố định ngoài vùng cuộn |
| `git diff --check` | PASS |

Lệnh đã dùng (build riêng để giữ các thư mục build sẵn có):

```powershell
& C:/Qt/6.11.1/mingw_64/bin/qt-cmake.bat -S . -B build-planning -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe
$env:PATH = 'C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.1\mingw_64\bin;' + $env:PATH
cmake --build build-planning --parallel 4
& C:/Qt/6.11.1/mingw_64/bin/windeployqt.exe --release --compiler-runtime ./build-planning/RBY1DesktopQt.exe
ctest --test-dir build-planning --output-on-failure
python tests/test_mock_planning.py
```

Configure báo thiếu WrapVulkanHeaders và C compiler flag không dùng (project chỉ CXX); build vẫn thành công. Deploy báo thiếu dxcompiler/dxil cho Direct3D 12; Widgets/Qt tests trong phạm vi này vẫn chạy thành công.

Artifacts local trong `build-planning/` (đã ignore): `RBY1DesktopQt.exe` cùng runtime, `test-results.xml`, `planning-results.xml`, `Testing/Temporary/LastTest.log`, `planning-panel-1200x840.png`, ảnh layout robot do test hiện có tạo.

Planning tests kiểm tra: request ID string/command/envelope correlation; response query đảo thứ tự; ACK không tạo plan; event sai ID, duplicate/lùi sequence và task cũ; terminal trước ACK; timeout/query response và terminal muộn; cancel ID riêng/ACK/race/timeout/disconnect; reconnect bắt buộc status đúng sequence trước scene và thao tác mới; scene/model/validation invalidation; capability execution/command gating; snapshot scene cũ đến muộn; NDJSON fragment/coalesce/UTF-8/CRLF/JSON sai/array/frame 1 MiB; input quaternion/scaling/distance/timeout; UI từ chối NaN/Inf/ô trống và lấy giá trị từ scene.

Qt integration tự khởi động Python mock với port động ở các mode `normal`, `terminal-first`, `failure`, `timeout`, `disconnect`, `stale-revision`, cancellation; bật fragment/coalesce. Python tests bổ sung BUSY, status retention/reconnect, TTL, preview không đổi scene live, unsupported execution/path và input không hữu hạn. Không gọi robot thật.

## File thay đổi

- Thêm `src/planning/PlanningClient.{hpp,cpp}`: socket v1, correlation, event/state, sync/reconnect, capability/scene/result/TTL.
- Thêm `src/planning/PlanningInput.{hpp,cpp}`: kiểm tra pose và planning payload.
- Thêm `src/ui/PlanningPanel.{hpp,cpp}`; sửa `src/ui/MainWindow.cpp` để gắn tab.
- Sửa `src/network/NdjsonParser.{hpp,cpp}`: giới hạn frame tùy chọn và discard/resync; default legacy không đổi. Không sửa `RobotClient`, `RobotController`, `RobotState` hoặc robot command protocol.
- Thêm `tools/mock_planning_server.py`, `tests/TestPlanning.cpp`, `tests/test_mock_planning.py`.
- Thêm `planning_protocol/protocol-v1.md`, `planning_protocol/fixtures/contract-v1.json`, báo cáo này.
- Sửa `CMakeLists.txt`, `README.md`, `.gitignore` để build/test/hướng dẫn/ignore build riêng.

Giữ nguyên các thay đổi đã tồn tại ở `question/ke-hoach-moveit2-rby1.md`, file untracked `question/prompts-test-moveit2-rby1.md` và toàn bộ thư mục untracked `motion_planning/`; không coi chúng là phần implementation mới của lần này.

## Chưa chạy — cần ROS 2 xác minh

Chưa kết nối endpoint planning ROS 2 được xác nhận trong phiên này. Vì vậy các mục sau **chưa kiểm chứng**, không báo PASS:

- ROS 2/MoveIt/RBY1 package/version/model/hash URDF/SRDF/config, group một tay, base/torso cố định, TCP/link/gripper/touch-links và TF/joint state thật trên fake hardware.
- Scene YAML/start-state fixture hiệu chỉnh: reachable baseline, obstacle chắn đường có đường vòng, start/goal collision, unreachable, joint limits, attached-object clearance, reset/repeat.
- OMPL/Cartesian/MTC thật, full Cartesian fraction, stage scene diffs/ACM, grasp transform và pose vật sau detach, retiming/interpolation validation theo giới hạn thực.
- RViz DisplayTrajectory/MTC introspection, object đi cùng tay, marker và preview không đổi scene live trên ROS.
- Contract integration Qt–ROS cho cancellation/deadline/TTL/reconnect và stale model/revision với worker MoveIt thật.

Mock chỉ trả metadata minh họa; `validation.simulated=true`, joint_limits/collision/timing=`not_checked`. Không có IK/collision/trajectory thật, không chứng minh grasp vật lý, ma sát hoặc chống trượt/rơi. Phần ROS phải triển khai service riêng đúng [protocol v1](protocol-v1.md) và thống nhất [fixture](fixtures/contract-v1.json), sau đó chạy lại integration/scene validation bằng fake hardware.
