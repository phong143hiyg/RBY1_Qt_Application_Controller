#include "sdk/SdkRobotClient.hpp"

#include <rby1-sdk/model.h>
#include <rby1-sdk/robot.h>
#include <rby1-sdk/robot_command_builder.h>

#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QMetaObject>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QtMath>

#include <array>
#include <atomic>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

namespace
{
using ModelA = rb::y1_model::A;
using ModelM = rb::y1_model::M;
using Token = std::shared_ptr<std::atomic_bool>;

template <typename Model>
bool matchesModel(const rb::RobotInfo &info)
{
    return info.degree_of_freedom == Model::kRobotDOF
        && info.mobility_joint_idx == std::vector<unsigned int>(Model::kMobilityIdx.begin(), Model::kMobilityIdx.end())
        && info.torso_joint_idx == std::vector<unsigned int>(Model::kTorsoIdx.begin(), Model::kTorsoIdx.end())
        && info.right_arm_joint_idx == std::vector<unsigned int>(Model::kRightArmIdx.begin(), Model::kRightArmIdx.end())
        && info.left_arm_joint_idx == std::vector<unsigned int>(Model::kLeftArmIdx.begin(), Model::kLeftArmIdx.end())
        && info.head_joint_idx == std::vector<unsigned int>(Model::kHeadIdx.begin(), Model::kHeadIdx.end());
}

struct SdkState
{
    Eigen::VectorXd position;
    rb::BatteryState battery_state;
};

class CommandHandle
{
public:
    template <typename Model>
    explicit CommandHandle(std::unique_ptr<rb::RobotCommandHandler<Model>> handle)
        : handle_(std::move(handle)) {}

    void Cancel() { std::visit([](auto &handle) { handle->Cancel(); }, handle_); }
    bool IsDone() const { return std::visit([](const auto &handle) { return handle->IsDone(); }, handle_); }
    bool GetStatus() const { return std::visit([](const auto &handle) { return handle->GetStatus(); }, handle_); }
    rb::RobotCommandFeedback Get() { return std::visit([](auto &handle) { return handle->Get(); }, handle_); }

private:
    std::variant<std::unique_ptr<rb::RobotCommandHandler<ModelA>>,
                 std::unique_ptr<rb::RobotCommandHandler<ModelM>>> handle_;
};

class CommandStream
{
public:
    template <typename Model>
    explicit CommandStream(std::unique_ptr<rb::RobotCommandStreamHandler<Model>> stream)
        : stream_(std::move(stream)) {}

    void Cancel() { std::visit([](auto &stream) { stream->Cancel(); }, stream_); }
    rb::RobotCommandFeedback SendCommand(const rb::RobotCommandBuilder &command, int timeoutMs)
    {
        return std::visit([&](auto &stream) { return stream->SendCommand(command, timeoutMs); }, stream_);
    }

private:
    std::variant<std::unique_ptr<rb::RobotCommandStreamHandler<ModelA>>,
                 std::unique_ptr<rb::RobotCommandStreamHandler<ModelM>>> stream_;
};

class Robot
{
    std::variant<std::shared_ptr<rb::Robot<ModelA>>, std::shared_ptr<rb::Robot<ModelM>>> robot_;

    template <typename Function>
    auto with(Function function) const
    {
        return std::visit([&](const auto &robot) { return function(*robot); }, robot_);
    }

public:
    template <typename Model>
    explicit Robot(std::shared_ptr<rb::Robot<Model>> robot) : robot_(std::move(robot)) {}

    static std::shared_ptr<Robot> CreateA(const std::string &address)
    {
        return std::make_shared<Robot>(rb::Robot<ModelA>::Create(address));
    }
    static std::shared_ptr<Robot> CreateM(const std::string &address)
    {
        return std::make_shared<Robot>(rb::Robot<ModelM>::Create(address));
    }

    bool Connect(int retries, int timeoutMs, const std::function<bool()> &check)
    {
        return with([&](auto &robot) { return robot.Connect(retries, timeoutMs, check); });
    }
    void Disconnect() { with([](auto &robot) { robot.Disconnect(); }); }
    bool IsConnected() const { return with([](auto &robot) { return robot.IsConnected(); }); }
    rb::RobotInfo GetRobotInfo() const { return with([](auto &robot) { return robot.GetRobotInfo(); }); }
    SdkState GetState() const
    {
        return with([](auto &robot) {
            const auto state = robot.GetState();
            return SdkState{Eigen::VectorXd(state.position), state.battery_state};
        });
    }
    rb::ControlManagerState GetControlManagerState() const
    {
        return with([](auto &robot) { return robot.GetControlManagerState(); });
    }
    bool IsPowerOn(const std::string &name) const { return with([&](auto &robot) { return robot.IsPowerOn(name); }); }
    bool IsServoOn(const std::string &name) const { return with([&](auto &robot) { return robot.IsServoOn(name); }); }
    bool PowerOn(const std::string &name) { return with([&](auto &robot) { return robot.PowerOn(name); }); }
    bool PowerOff(const std::string &name) { return with([&](auto &robot) { return robot.PowerOff(name); }); }
    bool ServoOn(const std::string &name) { return with([&](auto &robot) { return robot.ServoOn(name); }); }
    bool ServoOff(const std::string &name) { return with([&](auto &robot) { return robot.ServoOff(name); }); }
    bool EnableControlManager(bool unlimited) { return with([&](auto &robot) { return robot.EnableControlManager(unlimited); }); }
    bool DisableControlManager() { return with([](auto &robot) { return robot.DisableControlManager(); }); }
    bool ResetFaultControlManager() { return with([](auto &robot) { return robot.ResetFaultControlManager(); }); }
    bool CancelControl() { return with([](auto &robot) { return robot.CancelControl(); }); }
    std::unique_ptr<CommandHandle> SendCommand(const rb::RobotCommandBuilder &command)
    {
        return with([&](auto &robot) { return std::make_unique<CommandHandle>(robot.SendCommand(command)); });
    }
    std::unique_ptr<CommandStream> CreateCommandStream()
    {
        return with([](auto &robot) { return std::make_unique<CommandStream>(robot.CreateCommandStream()); });
    }
};

QJsonObject result(bool success, const QString &message = {})
{
    return {{QStringLiteral("success"), success}, {QStringLiteral("message"), message}};
}

QString motionFailureMessage(
    bool grpcOk,
    rb::RobotCommandFeedback::FinishCode finishCode)
{
    if (!grpcOk)
        return QStringLiteral("SDK command transport failed.");

    using FinishCode = rb::RobotCommandFeedback::FinishCode;
    switch (finishCode) {
    case FinishCode::kCanceled:
        return QStringLiteral("SDK motion was cancelled (finish_code=Canceled).");
    case FinishCode::kPreempted:
        return QStringLiteral("SDK motion was replaced by another command (finish_code=Preempted).");
    case FinishCode::kInitializationFailed:
        return QStringLiteral("SDK could not initialize the motion (finish_code=InitializationFailed). Check the target joint range.");
    case FinishCode::kControlManagerIdle:
        return QStringLiteral("SDK motion failed because Control Manager is idle.");
    case FinishCode::kControlManagerFault:
        return QStringLiteral("SDK motion failed because Control Manager is in Fault.");
    case FinishCode::kUnexpectedState:
        return QStringLiteral("SDK rejected the motion (finish_code=UnexpectedState). Check the target joint range and robot state.");
    case FinishCode::kUnknown:
        return QStringLiteral("SDK motion failed (finish_code=Unknown).");
    case FinishCode::kOk:
        break;
    }
    return QStringLiteral("SDK motion failed.");
}

QJsonObject component(bool known, bool enabled)
{
    return {{QStringLiteral("known"), known}, {QStringLiteral("enabled"), enabled},
            {QStringLiteral("pending"), false}, {QStringLiteral("source"), QStringLiteral("rby1-sdk")}};
}

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool isLoopbackHost(const QString &host)
{
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
        return true;

    QHostAddress address;
    return address.setAddress(host) && address.isLoopback();
}

void verifyTcpEndpoint(const QString &host, quint16 port, const QString &endpoint)
{
    QTcpSocket probe;
    probe.connectToHost(host, port);
    if (probe.waitForConnected(1500)) {
        probe.abort();
        return;
    }

    QString guidance;
    if (isLoopbackHost(host)) {
        guidance = QStringLiteral(
            "Không có RBY1 server trên máy này. Hãy khởi động simulator/server RBY1, "
            "chạy tools/start-rby1-sim.ps1 rồi kết nối 127.0.0.1:55051.");
    } else {
        guidance = QStringLiteral(
            "Hãy kiểm tra robot đã bật, máy tính cùng subnet với robot và firewall cho phép TCP 50051.");
    }

    const QString message = QStringLiteral("Không thể mở TCP tới RBY1 SDK tại %1: %2. %3")
                                .arg(endpoint, probe.errorString(), guidance);
    throw std::runtime_error(message.toUtf8().toStdString());
}
}

struct SdkRobotClient::Impl
{
    struct Request { QString operation; Token valid; };
    struct Motion
    {
        quint64 id;
        Token valid;
        std::unique_ptr<CommandHandle> handle;
    };

    SdkRobotClient *owner;
    QThread thread;
    QObject *worker = new QObject;
    QTimer *poll = nullptr;
    // SDK objects and measured state are accessed exclusively on thread.
    std::shared_ptr<Robot> robot;
    rb::RobotInfo info;
    std::optional<Motion> motion;
    std::unique_ptr<CommandStream> velocityStream;
    std::optional<Eigen::VectorXd> readyPose;
    Token session = std::make_shared<std::atomic_bool>(false);
    Token workerSession = session;
    // These fields are accessed exclusively on the GUI thread.
    QHash<quint64, Request> requests;
    quint64 nextId = 1;
    bool connected = false;
    bool connecting = false;
    bool velocityQueued = false;

    explicit Impl(SdkRobotClient *client) : owner(client)
    {
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
        QMetaObject::invokeMethod(worker, [this] {
            poll = new QTimer(worker);
            poll->setInterval(20);
            QObject::connect(poll, &QTimer::timeout, worker, [this] { pollMotion(); });
            poll->start();
        }, Qt::QueuedConnection);
    }

    ~Impl()
    {
        session->store(false);
        for (auto &request : requests)
            request.valid->store(false);
        QMetaObject::invokeMethod(worker, [this] { close(); }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }

    void close()
    {
        // Cleanup may run after a network failure. Each operation is best effort.
        try { if (motion) motion->handle->Cancel(); } catch (...) {}
        motion.reset();
        try { if (velocityStream) velocityStream->Cancel(); } catch (...) {}
        velocityStream.reset();
        try { if (robot) robot->Disconnect(); } catch (...) {}
        robot.reset();
        readyPose.reset();
    }

    void complete(quint64 id, const QJsonObject &response)
    {
        QMetaObject::invokeMethod(owner, [this, id, response] {
            auto it = requests.find(id);
            if (it == requests.end()) return;
            const QString operation = it->operation;
            if (operation == QStringLiteral("Velocity")) velocityQueued = false;
            it->valid->store(false);
            requests.erase(it);
            emit owner->responseReceived(id, operation, response);
        }, Qt::QueuedConnection);
    }

    using Operation = std::function<void(quint64, const Token &)>;
    quint64 submit(const QString &operation, int timeoutMs, Operation call)
    {
        if (!connected) {
            emit owner->clientError(QStringLiteral("Not connected to the RBY1 SDK endpoint."));
            return 0;
        }
        const quint64 id = nextId++;
        const auto valid = std::make_shared<std::atomic_bool>(true);
        const auto currentSession = session;
        requests.insert(id, {operation, valid});
        if (timeoutMs > 0) {
            QTimer::singleShot(timeoutMs, owner, [this, id, valid] {
                auto it = requests.find(id);
                if (it == requests.end()) return;
                const QString operation = it->operation;
                if (operation == QStringLiteral("Velocity")) velocityQueued = false;
                valid->store(false);
                requests.erase(it);
                QMetaObject::invokeMethod(worker, [this, id] {
                    if (motion && motion->id == id) {
                        try { motion->handle->Cancel(); } catch (...) {}
                    }
                }, Qt::QueuedConnection);
                emit owner->requestTimedOut(id, operation);
            });
        }
        QMetaObject::invokeMethod(worker, [this, id, valid, currentSession, call = std::move(call)] {
            if (!valid->load() || !currentSession->load()) return;
            try {
                require(robot && robot->IsConnected(), "SDK connection is no longer available.");
                call(id, valid);
            } catch (const std::exception &error) {
                complete(id, result(false, QString::fromUtf8(error.what())));
                checkConnection(currentSession);
            }
        }, Qt::QueuedConnection);
        return id;
    }

    void checkConnection(const Token &currentSession)
    {
        if (robot && robot->IsConnected()) return;
        currentSession->store(false);
        QMetaObject::invokeMethod(owner, [this, currentSession] {
            if (session != currentSession || !connected) return;
            connected = false;
            for (auto &request : requests) request.valid->store(false);
            requests.clear();
            emit owner->robotDisconnected();
        }, Qt::QueuedConnection);
    }

    const std::vector<unsigned int> &indices(const QString &group) const
    {
        if (group == QStringLiteral("torso")) return info.torso_joint_idx;
        if (group == QStringLiteral("right_arm")) return info.right_arm_joint_idx;
        if (group == QStringLiteral("left_arm")) return info.left_arm_joint_idx;
        if (group == QStringLiteral("head")) return info.head_joint_idx;
        throw std::runtime_error("Unknown joint group.");
    }

    Eigen::VectorXd measuredPositions()
    {
        const auto state = robot->GetState();
        require(state.position.size() == info.degree_of_freedom && state.position.allFinite(),
                "SDK returned invalid joint positions.");
        return state.position;
    }

    std::unique_ptr<rb::RobotCommandBuilder> groupCommand(const QString &group, const Eigen::VectorXd &all)
    {
        const auto &idx = indices(group);
        require(!idx.empty(), "Robot does not have the requested joint group.");
        Eigen::VectorXd q(idx.size());
        for (size_t i = 0; i < idx.size(); ++i) {
            require(idx[i] < all.size(), "SDK joint index is outside the position vector.");
            q[i] = all[idx[i]];
        }
        rb::JointPositionCommandBuilder position;
        const Eigen::VectorXd velocityLimit =
            Eigen::VectorXd::Constant(q.size(), 1.0); // rad/s

        const Eigen::VectorXd accelerationLimit =
            Eigen::VectorXd::Constant(q.size(), 2.0); // rad/s²

        position
                .SetPosition(q);
                .SetVeclocityLimit(velocityLimit);
                .SetAccelerationLimit(accelerationLimit);
        rb::ComponentBasedCommandBuilder components;
        if (group == QStringLiteral("head")) {
            components.SetHeadCommand(rb::HeadCommandBuilder().SetCommand(position));
        } else {
            rb::BodyComponentBasedCommandBuilder body;
            if (group == QStringLiteral("torso"))
                body.SetTorsoCommand(rb::TorsoCommandBuilder().SetCommand(position));
            else if (group == QStringLiteral("right_arm"))
                body.SetRightArmCommand(rb::ArmCommandBuilder().SetCommand(position));
            else
                body.SetLeftArmCommand(rb::ArmCommandBuilder().SetCommand(position));
            components.SetBodyCommand(rb::BodyCommandBuilder(body));
        }
        auto command = std::make_unique<rb::RobotCommandBuilder>();
        command->SetCommand(components);
        return command;
    }

    void startMotion(quint64 id, const Token &valid, const rb::RobotCommandBuilder &command)
    {
        require(!motion, "Another SDK motion is still pending.");
        require(robot->GetControlManagerState().state == rb::ControlManagerState::State::kEnabled,
                "Control Manager must be enabled before moving.");
        if (!valid->load()) return;
        auto handle = robot->SendCommand(command);
        require(static_cast<bool>(handle), "SDK did not return a command handle.");
        motion.emplace(Motion{id, valid, std::move(handle)});
    }

    void pollMotion()
    {
        if (!motion) return;
        const quint64 id = motion->id;
        try {
            if (!motion->valid->load()) motion->handle->Cancel();
            if (!motion->handle->IsDone()) return;
            const bool grpcOk = motion->handle->GetStatus();
            const auto feedback = motion->handle->Get();
            motion.reset();
            const bool success = grpcOk && feedback.finish_code() == rb::RobotCommandFeedback::FinishCode::kOk;
            complete(id, result(success, success
                ? QString{}
                : motionFailureMessage(grpcOk, feedback.finish_code())));
        } catch (const std::exception &error) {
            motion.reset();
            complete(id, result(false, QString::fromUtf8(error.what())));
            checkConnection(workerSession);
        }
    }

    void velocity(double x, double y, double angularZ)
    {
        require(qIsFinite(x) && qIsFinite(y) && qIsFinite(angularZ), "Invalid base velocity.");
        require(!motion, "Cannot drive while a joint motion is pending.");
        require(robot->GetControlManagerState().state == rb::ControlManagerState::State::kEnabled,
                "Control Manager must be enabled before driving.");
        if (!velocityStream) velocityStream = robot->CreateCommandStream();
        require(static_cast<bool>(velocityStream), "Could not create SDK command stream.");
        rb::SE2VelocityCommandBuilder velocity;
        velocity.SetCommandHeader(rb::CommandHeaderBuilder().SetControlHoldTime(0.30))
                .SetVelocity(Eigen::Vector2d(x, y), angularZ);
        rb::ComponentBasedCommandBuilder components;
        components.SetMobilityCommand(rb::MobilityCommandBuilder().SetCommand(velocity));
        velocityStream->SendCommand(rb::RobotCommandBuilder().SetCommand(components), 500);
    }
};

SdkRobotClient::SdkRobotClient(QObject *parent) : RobotClient(parent), impl_(std::make_unique<Impl>(this)) {}
SdkRobotClient::~SdkRobotClient() = default;
bool SdkRobotClient::isConnected() const { return impl_->connected; }

void SdkRobotClient::connectToRobot(const QString &host, quint16 port, Rby1Model model)
{
    auto &p = *impl_;
    if (p.connected || p.connecting) return;
    const QString address = QStringLiteral("%1:%2").arg(host.contains(QLatin1Char(':'))
        ? QStringLiteral("[%1]").arg(host) : host).arg(port);
    p.connecting = true;
    const auto session = std::make_shared<std::atomic_bool>(true);
    p.session = session;
    QMetaObject::invokeMethod(p.worker, [this, host, port, address, model, session] {
        auto &p = *impl_;
        try {
            p.close();
            p.workerSession = session;
            if (!session->load()) return;
            verifyTcpEndpoint(host, port, address);
            if (!session->load()) return;
            p.robot = model == Rby1Model::M
                ? Robot::CreateM(address.toStdString())
                : Robot::CreateA(address.toStdString());
            require(p.robot->Connect(3, 1500, [session] { return session->load(); }),
                    "TCP is open, but the RBY1 gRPC service did not become ready. Check that this endpoint is an RBY1 robot/simulator.");
            p.info = p.robot->GetRobotInfo();
            const bool selectedModelMatches = model == Rby1Model::M
                ? matchesModel<ModelM>(p.info)
                : matchesModel<ModelA>(p.info);
            const char *expectedModel = model == Rby1Model::M ? "RBY1-M" : "RBY1-A";
            require(selectedModelMatches,
                    "Robot model/joint layout does not match the selected " + std::string(expectedModel)
                        + ": endpoint reported " + p.info.robot_model_name
                        + " (" + std::to_string(p.info.degree_of_freedom) + " joints).");
            QMetaObject::invokeMethod(this, [this, session] {
                auto &p = *impl_;
                if (p.session != session || !session->load()) return;
                p.connecting = false;
                p.connected = true;
                emit robotConnected();
            }, Qt::QueuedConnection);
        } catch (const std::exception &error) {
            const QString message = QString::fromUtf8(error.what());
            p.close();
            QMetaObject::invokeMethod(this, [this, session, message] {
                if (impl_->session != session || !session->load()) return;
                impl_->connecting = false;
                session->store(false);
                emit clientError(message);
            }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
}

void SdkRobotClient::disconnectFromRobot()
{
    auto &p = *impl_;
    p.session->store(false);
    p.connecting = false;
    const bool wasConnected = p.connected;
    p.connected = false;
    p.velocityQueued = false;
    for (auto &request : p.requests) request.valid->store(false);
    p.requests.clear();
    QMetaObject::invokeMethod(p.worker, [this] { impl_->close(); }, Qt::QueuedConnection);
    if (wasConnected) emit robotDisconnected();
}

quint64 SdkRobotClient::readStatus(int timeoutMs)
{
    return impl_->submit(QStringLiteral("Status"), timeoutMs, [this](quint64 id, const Token &) {
        const auto state = impl_->robot->GetState();
        const auto manager = impl_->robot->GetControlManagerState();
        const bool power = impl_->robot->IsPowerOn(".*");
        const bool servo = impl_->robot->IsServoOn(".*");
        const bool managerKnown = manager.state != rb::ControlManagerState::State::kUnknown;
        const bool enabled = manager.state == rb::ControlManagerState::State::kEnabled;
        const bool fault = manager.state == rb::ControlManagerState::State::kMinorFault
            || manager.state == rb::ControlManagerState::State::kMajorFault;
        QJsonObject response = result(true);
        response.insert(QStringLiteral("connected"), true);
        response.insert(QStringLiteral("components"), QJsonObject{
            {QStringLiteral("power"), component(true, power)},
            {QStringLiteral("servo"), component(true, servo)},
            {QStringLiteral("stream"), component(managerKnown, enabled)}});
        response.insert(QStringLiteral("status"), QJsonObject{
            {QStringLiteral("ready"), power && servo && enabled && !fault},
            {QStringLiteral("state"), fault ? QStringLiteral("Fault") : QStringLiteral("SDK connected")},
            {QStringLiteral("battery_voltage"), state.battery_state.voltage},
            {QStringLiteral("battery_level"), state.battery_state.level_percent},
            {QStringLiteral("ready_pose_saved"), impl_->readyPose.has_value()}});
        impl_->complete(id, response);
    });
}

quint64 SdkRobotClient::readJoints(int timeoutMs)
{
    return impl_->submit(QStringLiteral("Joints status"), timeoutMs, [this](quint64 id, const Token &) {
        const auto positions = impl_->measuredPositions();
        QJsonObject groups;
        for (const auto &name : {QStringLiteral("torso"), QStringLiteral("right_arm"),
                                 QStringLiteral("left_arm"), QStringLiteral("head")}) {
            QJsonArray values;
            for (const auto index : impl_->indices(name)) values.append(positions[index]);
            groups.insert(name, QJsonObject{{QStringLiteral("positions"), values}});
        }
        QJsonObject response = result(true);
        response.insert(QStringLiteral("groups"), groups);
        impl_->complete(id, response);
    });
}

quint64 SdkRobotClient::setComponent(RobotComponent component, bool enabled,
                                    const QString &operationName, int timeoutMs)
{
    return impl_->submit(operationName, timeoutMs, [this, component, enabled](quint64 id, const Token &) {
        const auto &robot = impl_->robot;
        bool ok;
        switch (component) {
        case RobotComponent::Power: ok = enabled ? robot->PowerOn(".*") : robot->PowerOff(".*"); break;
        case RobotComponent::Servo: ok = enabled ? robot->ServoOn(".*") : robot->ServoOff(".*"); break;
        case RobotComponent::Stream:
            if (!enabled) {
                ok = robot->DisableControlManager();
                break;
            }

            // The simulator can start in MinorFault/MajorFault. The SDK rejects
            // COMMAND_ENABLE until the fault has first been reset to Idle.
            {
                const auto manager = robot->GetControlManagerState();
                if (manager.state == rb::ControlManagerState::State::kMinorFault
                    || manager.state == rb::ControlManagerState::State::kMajorFault) {
                    require(robot->ResetFaultControlManager(),
                            "Control Manager is in Fault and the SDK could not reset it.");
                }
            }
            ok = robot->EnableControlManager(false);
            break;
        default: throw std::runtime_error("Unknown system component.");
        }
        impl_->complete(id, result(ok, ok ? QString{} : QStringLiteral("SDK rejected the system operation.")));
    });
}

quint64 SdkRobotClient::moveJointRelative(const QString &group, int index, double delta,
                                         int timeoutMs)
{
    return impl_->submit(QStringLiteral("Joint nudge"), timeoutMs, [this, group, index, delta](quint64 id, const Token &valid) {
        require(qIsFinite(delta), "Invalid joint delta.");
        const auto &indices = impl_->indices(group);
        require(index >= 0 && index < static_cast<int>(indices.size()), "Joint index is outside the group.");
        auto positions = impl_->measuredPositions();
        positions[indices[index]] += delta;
        impl_->startMotion(id, valid, *impl_->groupCommand(group, positions));
    });
}

quint64 SdkRobotClient::executePose(const QString &pose, const QString &operation, int timeoutMs)
{
    return impl_->submit(operation, timeoutMs, [this, pose](quint64 id, const Token &valid) {
        if (pose == QStringLiteral("set_ready_pose")) {
            impl_->readyPose = impl_->measuredPositions();
            impl_->complete(id, result(true));
            return;
        }
        if (pose == QStringLiteral("clear_ready_pose")) {
            impl_->readyPose.reset();
            impl_->complete(id, result(true));
            return;
        }
        require(pose == QStringLiteral("zero_pose") || pose == QStringLiteral("ready_pose")
                || pose == QStringLiteral("arms_ready"), "Unknown pose operation.");
        require(pose != QStringLiteral("ready_pose") || impl_->readyPose.has_value(),
                "Save a ready pose in this SDK session before requesting Go Pose.");

        Eigen::VectorXd target;
        if (pose == QStringLiteral("zero_pose")) {
            target = Eigen::VectorXd::Zero(impl_->info.degree_of_freedom);
        } else if (pose == QStringLiteral("ready_pose")) {
            target = *impl_->readyPose;
        } else {
            // Standard RBY1-M/A folded-arm pose from the manufacturer's SDK
            // examples. It is independent of the user SET POSE / GO POSE slot.
            target = impl_->measuredPositions();
            const std::array<double, 7> rightArm{
                0.0, qDegreesToRadians(-5.0), 0.0, qDegreesToRadians(-120.0),
                0.0, qDegreesToRadians(70.0), 0.0};
            const std::array<double, 7> leftArm{
                0.0, qDegreesToRadians(5.0), 0.0, qDegreesToRadians(-120.0),
                0.0, qDegreesToRadians(70.0), 0.0};
            const auto &rightIndices = impl_->indices(QStringLiteral("right_arm"));
            const auto &leftIndices = impl_->indices(QStringLiteral("left_arm"));
            require(rightIndices.size() == rightArm.size() && leftIndices.size() == leftArm.size(),
                    "The connected robot does not have the expected 7-DOF arms.");
            for (size_t i = 0; i < rightArm.size(); ++i) {
                target[rightIndices[i]] = rightArm[i];
                target[leftIndices[i]] = leftArm[i];
            }
        }
        rb::BodyComponentBasedCommandBuilder body;
        auto positionFor = [this, &target](const QString &name) {
            const auto &indices = impl_->indices(name);
            Eigen::VectorXd q(indices.size());
            for (size_t i = 0; i < indices.size(); ++i) q[i] = target[indices[i]];
            auto position = std::make_unique<rb::JointPositionCommandBuilder>();
            position->SetPosition(q);
            return position;
        };
        body.SetRightArmCommand(rb::ArmCommandBuilder().SetCommand(*positionFor(QStringLiteral("right_arm"))));
        body.SetLeftArmCommand(rb::ArmCommandBuilder().SetCommand(*positionFor(QStringLiteral("left_arm"))));
        rb::ComponentBasedCommandBuilder components;
        if (pose != QStringLiteral("arms_ready")) {
            body.SetTorsoCommand(rb::TorsoCommandBuilder().SetCommand(*positionFor(QStringLiteral("torso"))));
            components.SetHeadCommand(rb::HeadCommandBuilder().SetCommand(*positionFor(QStringLiteral("head"))));
        }
        components.SetBodyCommand(rb::BodyCommandBuilder(body));
        impl_->startMotion(id, valid, rb::RobotCommandBuilder().SetCommand(components));
    });
}

quint64 SdkRobotClient::executeSimple(const QString &action, const QString &operation, int timeoutMs)
{
    if (action == QStringLiteral("set_ready_pose") || action == QStringLiteral("clear_ready_pose"))
        return executePose(action, operation, timeoutMs);
    return impl_->submit(operation, timeoutMs, [this, action](quint64 id, const Token &) {
        bool ok = true;
        if (action == QStringLiteral("ping")) {
            impl_->robot->GetRobotInfo();
        } else if (action == QStringLiteral("cancel")) {
            if (impl_->motion) impl_->motion->handle->Cancel();
            if (impl_->velocityStream) impl_->velocityStream->Cancel();
            impl_->velocityStream.reset();
            ok = impl_->robot->CancelControl();
        } else if (action == QStringLiteral("stop")) {
            if (impl_->velocityStream) {
                impl_->velocity(0.0, 0.0, 0.0);
                impl_->velocityStream->Cancel();
                impl_->velocityStream.reset();
            }
        } else {
            throw std::runtime_error("Unsupported robot SDK action.");
        }
        impl_->complete(id, result(ok));
    });
}

quint64 SdkRobotClient::setVelocity(double x, double y, double angularZ)
{
    // Coalesce timer ticks if an SDK call takes longer than the 100 ms UI tick.
    if (!isConnected() || impl_->velocityQueued) return 0;
    impl_->velocityQueued = true;
    const auto id = impl_->submit(QStringLiteral("Velocity"), 1000, [this, x, y, angularZ](quint64 id, const Token &) {
        impl_->velocity(x, y, angularZ);
        impl_->complete(id, result(true));
    });
    if (!id) impl_->velocityQueued = false;
    return id;
}
