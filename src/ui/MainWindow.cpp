#include "ui/MainWindow.hpp"
#include "ui/ToggleSwitch.hpp"

#include "controller/RobotController.hpp"

#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStringList>
#include <QTabWidget>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
constexpr double kPi = 3.14159265358979323846;

double degreesToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

double radiansToDegrees(double radians)
{
    return radians * 180.0 / kPi;
}

QString statusValueText(const QJsonValue &value)
{
    if (value.isBool())
    {
        return value.toBool()
            ? QStringLiteral("true")
            : QStringLiteral("false");
    }

    if (value.isDouble())
    {
        return QString::number(value.toDouble(), 'g', 6);
    }

    if (value.isString())
    {
        return value.toString();
    }

    return QStringLiteral("null");
}

QJsonValue findStatusValue(
    const QJsonObject &response,
    const QJsonObject &status,
    const QStringList &keys)
{
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

    return {};
}

QString compactStatusText(const QJsonObject &response)
{
    const QJsonObject status =
        response.value(QStringLiteral("status")).toObject();

    QStringList parts;

    const auto appendField =
        [&parts, &response, &status](
            const QString &label,
            const QStringList &keys)
        {
            const QJsonValue value =
                findStatusValue(response, status, keys);

            if (!value.isUndefined()
                && !value.isObject()
                && !value.isArray())
            {
                parts.append(
                    QStringLiteral("%1=%2")
                        .arg(label, statusValueText(value)));
            }
        };

    appendField(
        QStringLiteral("success"),
        {QStringLiteral("success")});
    appendField(
        QStringLiteral("state"),
        {
            QStringLiteral("state"),
            QStringLiteral("robot_state"),
            QStringLiteral("control_state")
        });
    appendField(
        QStringLiteral("ready"),
        {QStringLiteral("ready")});
    appendField(
        QStringLiteral("power"),
        {
            QStringLiteral("power"),
            QStringLiteral("power_on"),
            QStringLiteral("powered")
        });
    appendField(
        QStringLiteral("servo"),
        {
            QStringLiteral("servo"),
            QStringLiteral("servo_on"),
            QStringLiteral("servo_enabled")
        });
    appendField(
        QStringLiteral("stream"),
        {
            QStringLiteral("stream"),
            QStringLiteral("stream_on"),
            QStringLiteral("streaming"),
            QStringLiteral("stream_enabled")
        });
    appendField(
        QStringLiteral("message"),
        {
            QStringLiteral("message"),
            QStringLiteral("error")
        });

    if (parts.isEmpty())
    {
        return QStringLiteral(
            "Trạng thái: đã nhận phản hồi, không có trường chính.");
    }

    return QStringLiteral("Trạng thái: %1")
        .arg(parts.join(QStringLiteral(" | ")));
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      controller_(new RobotController(this))
{
    buildInterface();
    connectSignals();

    applyControllerState(
        QStringLiteral("Disconnected"),
        false,
        false,
        false,
        false,
        false);
}

void MainWindow::buildInterface()
{
    setWindowTitle(
        QStringLiteral(
            "RBY1 Desktop Controller - State Pattern"));

    resize(1200, 840);

    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(centralWidget);

    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(10);

    auto *titleLabel =
        new QLabel(
            QStringLiteral(
                "RBY1 Desktop Controller"),
            centralWidget);

    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(20);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    auto *statusLayout = new QHBoxLayout();

    connectionStatusLabel_ =
        new QLabel(
            QStringLiteral(
                "Kết nối: Chưa kết nối"),
            centralWidget);

    stateLabel_ =
        new QLabel(
            QStringLiteral(
                "State: Disconnected"),
            centralWidget);

    QFont stateFont = stateLabel_->font();
    stateFont.setBold(true);
    stateLabel_->setFont(stateFont);

    statusLayout->addWidget(connectionStatusLabel_);
    statusLayout->addStretch();
    statusLayout->addWidget(stateLabel_);

    auto *connectionGroup =
        new QGroupBox(
            QStringLiteral("Kết nối bridge"),
            centralWidget);

    auto *connectionLayout =
        new QHBoxLayout(connectionGroup);

    connectButton_ =
        new QPushButton(
            QStringLiteral("Kết nối"),
            connectionGroup);

    pingButton_ =
        new QPushButton(
            QStringLiteral("Ping"),
            connectionGroup);

    logButton_ =
        new QPushButton(
            QStringLiteral("Log"),
            connectionGroup);

    connectionLayout->addWidget(connectButton_);
    connectionLayout->addWidget(pingButton_);
    connectionLayout->addWidget(logButton_);
    connectionLayout->addStretch();

    tabWidget_ = new QTabWidget(centralWidget);

    tabWidget_->addTab(
        buildSystemAndBaseTab(),
        QStringLiteral("Điều khiển robot"));

    logWindow_ = new QDialog(this);
    logWindow_->setWindowTitle(
        QStringLiteral("Log robot"));
    logWindow_->resize(820, 520);

    auto *logLayout = new QVBoxLayout(logWindow_);

    logTextEdit_ = new QTextEdit(logWindow_);
    logTextEdit_->setReadOnly(true);
    logTextEdit_->document()->setMaximumBlockCount(5000);

    QFont logFont(QStringLiteral("Consolas"));
    logFont.setStyleHint(QFont::Monospace);
    logTextEdit_->setFont(logFont);
    logLayout->addWidget(logTextEdit_);

    mainLayout->addWidget(titleLabel);
    mainLayout->addLayout(statusLayout);
    mainLayout->addWidget(connectionGroup);
    mainLayout->addWidget(tabWidget_, 1);

    setCentralWidget(centralWidget);
}

QWidget *MainWindow::buildSystemAndBaseTab()
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    auto *topLayout = new QHBoxLayout();

    systemGroup_ =
        new QGroupBox(
            QStringLiteral(
                "Nguồn, servo và stream"),
            tab);

    auto *systemLayout =
        new QGridLayout(systemGroup_);

    prepareButton_ =
        new QPushButton(
            QStringLiteral("CHUẨN BỊ ROBOT"),
            systemGroup_);

    prepareButton_->setMinimumHeight(42);
    prepareButton_->setToolTip(
        QStringLiteral(
            "Bật nhanh Power, Servo và Stream rồi đưa robot vào Ready."));

    powerSwitch_ =
        new ToggleSwitch(
            QStringLiteral("Power"),
            systemGroup_);

    servoSwitch_ =
        new ToggleSwitch(
            QStringLiteral("Servo"),
            systemGroup_);

    streamSwitch_ =
        new ToggleSwitch(
            QStringLiteral("Stream"),
            systemGroup_);

    cancelButton_ =
        new QPushButton(
            QStringLiteral("CANCEL"),
            systemGroup_);

    cancelButton_->setStyleSheet(
        QStringLiteral(
            "QPushButton {"
            "background-color:#8b0000;"
            "color:white;"
            "font-weight:bold;"
            "}"));

    systemLayout->addWidget(
        prepareButton_, 0, 0, 1, 2);

    systemLayout->addWidget(
        powerSwitch_, 1, 0, 1, 2);

    systemLayout->addWidget(
        servoSwitch_, 2, 0, 1, 2);

    systemLayout->addWidget(
        streamSwitch_, 3, 0, 1, 2);

    systemLayout->addWidget(
        cancelButton_, 4, 0, 1, 2);

    systemLayout->setRowStretch(5, 1);

    driveGroup_ =
        new QGroupBox(
            QStringLiteral(
                "Điều khiển đế robot"),
            tab);

    auto *driveLayout =
        new QGridLayout(driveGroup_);

    rotateLeftButton_ =
        new QPushButton(
            QStringLiteral("↶ Xoay trái"),
            driveGroup_);

    forwardButton_ =
        new QPushButton(
            QStringLiteral("↑ Tiến"),
            driveGroup_);

    rotateRightButton_ =
        new QPushButton(
            QStringLiteral("Xoay phải ↷"),
            driveGroup_);

    leftButton_ =
        new QPushButton(
            QStringLiteral("↖ Rẽ trái"),
            driveGroup_);

    stopButton_ =
        new QPushButton(
            QStringLiteral("DỪNG"),
            driveGroup_);

    rightButton_ =
        new QPushButton(
            QStringLiteral("Rẽ phải ↗"),
            driveGroup_);

    backwardButton_ =
        new QPushButton(
            QStringLiteral("↓ Lùi"),
            driveGroup_);

    stopButton_->setStyleSheet(
        QStringLiteral(
            "QPushButton {"
            "background-color:#cd5c5c;"
            "color:white;"
            "font-weight:bold;"
            "}"));

    const QList<QPushButton *> buttons{
        rotateLeftButton_,
        forwardButton_,
        rotateRightButton_,
        leftButton_,
        stopButton_,
        rightButton_,
        backwardButton_
    };

    for (QPushButton *button : buttons)
    {
        button->setMinimumSize(96, 48);
    }

    driveLayout->addWidget(
        rotateLeftButton_, 0, 0);

    driveLayout->addWidget(
        forwardButton_, 0, 1);

    driveLayout->addWidget(
        rotateRightButton_, 0, 2);

    driveLayout->addWidget(
        leftButton_, 1, 0);

    driveLayout->addWidget(
        stopButton_, 1, 1);

    driveLayout->addWidget(
        rightButton_, 1, 2);

    driveLayout->addWidget(
        backwardButton_, 2, 1);

    robotStatusGroup_ =
        new QGroupBox(
            QStringLiteral("Trạng thái robot hiện tại"),
            tab);

    auto *robotStatusLayout =
        new QGridLayout(robotStatusGroup_);

    robotConnectionValueLabel_ = new QLabel(QStringLiteral("Chưa kết nối"), robotStatusGroup_);
    robotControllerStateValueLabel_ = new QLabel(QStringLiteral("Disconnected"), robotStatusGroup_);
    robotBridgeStateValueLabel_ = new QLabel(QStringLiteral("—"), robotStatusGroup_);
    robotReadyValueLabel_ = new QLabel(QStringLiteral("—"), robotStatusGroup_);
    robotPowerValueLabel_ = new QLabel(QStringLiteral("Tắt"), robotStatusGroup_);
    robotServoValueLabel_ = new QLabel(QStringLiteral("Tắt"), robotStatusGroup_);
    robotStreamValueLabel_ = new QLabel(QStringLiteral("Tắt"), robotStatusGroup_);
    robotLastUpdateValueLabel_ = new QLabel(QStringLiteral("—"), robotStatusGroup_);
    robotMessageValueLabel_ = new QLabel(QStringLiteral("—"), robotStatusGroup_);
    robotMessageValueLabel_->setWordWrap(true);

    const QList<QPair<QString, QLabel *>> statusRows{
        {QStringLiteral("Kết nối:"), robotConnectionValueLabel_},
        {QStringLiteral("State ứng dụng:"), robotControllerStateValueLabel_},
        {QStringLiteral("State bridge:"), robotBridgeStateValueLabel_},
        {QStringLiteral("Ready:"), robotReadyValueLabel_},
        {QStringLiteral("Power:"), robotPowerValueLabel_},
        {QStringLiteral("Servo:"), robotServoValueLabel_},
        {QStringLiteral("Stream:"), robotStreamValueLabel_},
        {QStringLiteral("Cập nhật lúc:"), robotLastUpdateValueLabel_},
        {QStringLiteral("Thông báo:"), robotMessageValueLabel_}
    };

    for (int row = 0; row < statusRows.size(); ++row)
    {
        auto *nameLabel =
            new QLabel(statusRows.at(row).first, robotStatusGroup_);
        nameLabel->setStyleSheet(
            QStringLiteral("font-weight:600;"));

        robotStatusLayout->addWidget(nameLabel, row, 0);
        robotStatusLayout->addWidget(statusRows.at(row).second, row, 1);
    }

    robotStatusLayout->setColumnStretch(1, 1);
    robotStatusLayout->setRowStretch(statusRows.size(), 1);

    topLayout->addWidget(systemGroup_, 1);
    topLayout->addWidget(driveGroup_, 2);
    topLayout->addWidget(robotStatusGroup_, 2);

    layout->addLayout(topLayout);
    layout->addWidget(buildUpperBodyTab(), 1);

    return tab;
}

QWidget *MainWindow::buildUpperBodyTab()
{
    auto *tab = new QWidget();

    upperBodyContent_ = new QWidget(tab);

    auto *tabLayout = new QVBoxLayout(tab);
    auto *layout = new QVBoxLayout(upperBodyContent_);

    auto *toolbarGroup =
        new QGroupBox(
            QStringLiteral(
                "Điều khiển thân trên"),
            upperBodyContent_);

    auto *toolbarLayout =
        new QGridLayout(toolbarGroup);

    initialButton_ =
        new QPushButton(
            QStringLiteral("INITIAL"),
            toolbarGroup);

    armsReadyButton_ =
        new QPushButton(
            QStringLiteral("Co hai tay"),
            toolbarGroup);

    setReadyButton_ =
        new QPushButton(
            QStringLiteral("SET POSE"),
            toolbarGroup);

    goReadyButton_ =
        new QPushButton(
            QStringLiteral("GO POSE"),
            toolbarGroup);

    clearReadyButton_ =
        new QPushButton(
            QStringLiteral("CLEAR POSE"),
            toolbarGroup);

    initialButton_->setToolTip(
        QStringLiteral("Đưa robot về tư thế ban đầu."));
    setReadyButton_->setToolTip(
        QStringLiteral("Lưu toàn bộ vị trí khớp hiện tại làm pose."));
    goReadyButton_->setToolTip(
        QStringLiteral("Đưa robot về pose đã lưu."));
    clearReadyButton_->setToolTip(
        QStringLiteral("Xóa pose đã lưu."));

    jointStepSpinBox_ =
        new QDoubleSpinBox(toolbarGroup);

    // The UI is in degrees; RobotController/bridge continue to use radians.
    jointStepSpinBox_->setRange(
        0.01 * 180.0 / kPi,
        0.20 * 180.0 / kPi);
    jointStepSpinBox_->setDecimals(2);
    jointStepSpinBox_->setSingleStep(0.1);
    jointStepSpinBox_->setValue(0.05 * 180.0 / kPi);
    jointStepSpinBox_->setSuffix(
        QStringLiteral(" \u00B0"));

    connect(
        jointStepSpinBox_,
        QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this,
        [this](double stepDegrees)
        {
            const int span =
                static_cast<int>(stepDegrees * 100.0);

            for (auto groupIt = jointSliders_.begin();
                 groupIt != jointSliders_.end();
                 ++groupIt)
            {
                for (QSlider *slider : groupIt.value())
                {
                    const int center =
                        static_cast<int>(
                            slider->property("confirmedDegrees").toDouble()
                            * 100.0);
                    slider->setRange(center - span, center + span);
                    slider->setValue(center);
                }
            }
        });

    minimumTimeSpinBox_ =
        new QDoubleSpinBox(toolbarGroup);

    minimumTimeSpinBox_->setRange(1.0, 10.0);
    minimumTimeSpinBox_->setDecimals(1);
    minimumTimeSpinBox_->setSingleStep(0.5);
    minimumTimeSpinBox_->setValue(5.0);
    minimumTimeSpinBox_->setSuffix(
        QStringLiteral(" s"));

    toolbarLayout->addWidget(
        new QLabel(
            QStringLiteral("Bước:"),
            toolbarGroup),
        0,
        0);

    toolbarLayout->addWidget(
        jointStepSpinBox_,
        0,
        1);

    toolbarLayout->addWidget(
        new QLabel(
            QStringLiteral("Thời gian:"),
            toolbarGroup),
        0,
        2);

    toolbarLayout->addWidget(
        minimumTimeSpinBox_,
        0,
        3);

    toolbarLayout->addWidget(initialButton_, 1, 0);

    toolbarLayout->addWidget(
        armsReadyButton_,
        1,
        1);

    toolbarLayout->addWidget(setReadyButton_, 1, 2);

    toolbarLayout->addWidget(goReadyButton_, 1, 3);

    toolbarLayout->addWidget(clearReadyButton_, 1, 4);

    auto *scrollArea =
        new QScrollArea(upperBodyContent_);

    scrollArea->setWidgetResizable(true);

    auto *scrollContent =
        new QWidget(scrollArea);

    auto *jointGrid =
        new QGridLayout(scrollContent);

    jointGrid->addWidget(
        buildJointGroup(
            QStringLiteral("Torso"),
            QStringLiteral("torso"),
            6),
        0,
        0);

    jointGrid->addWidget(
        buildJointGroup(
            QStringLiteral("Head"),
            QStringLiteral("head"),
            2),
        0,
        1);

    jointGrid->addWidget(
        buildJointGroup(
            QStringLiteral("Right Arm"),
            QStringLiteral("right_arm"),
            7),
        1,
        0);

    jointGrid->addWidget(
        buildJointGroup(
            QStringLiteral("Left Arm"),
            QStringLiteral("left_arm"),
            7),
        1,
        1);

    jointGrid->setColumnStretch(0, 1);
    jointGrid->setColumnStretch(1, 1);

    scrollArea->setWidget(scrollContent);

    layout->addWidget(toolbarGroup);
    layout->addWidget(scrollArea, 1);

    tabLayout->addWidget(upperBodyContent_);

    return tab;
}

QGroupBox *MainWindow::buildJointGroup(
    const QString &title,
    const QString &groupName,
    int jointCount)
{
    auto *groupBox = new QGroupBox(title);
    auto *layout = new QGridLayout(groupBox);

    auto *jointHeader =
        new QLabel(
            QStringLiteral("Khớp"),
            groupBox);

    auto *positionHeader =
        new QLabel(
            QStringLiteral("Vị trí hiện tại"),
            groupBox);

    auto *controlHeader =
        new QLabel(
            QStringLiteral("Điều chỉnh"),
            groupBox);

    QFont headerFont = jointHeader->font();
    headerFont.setBold(true);

    jointHeader->setFont(headerFont);
    positionHeader->setFont(headerFont);
    controlHeader->setFont(headerFont);

    layout->addWidget(jointHeader, 0, 0);
    layout->addWidget(positionHeader, 0, 1);
    layout->addWidget(controlHeader, 0, 2, 1, 2);

    QVector<QLabel *> labels;
    labels.reserve(jointCount);

    QVector<QSlider *> sliders;
    sliders.reserve(jointCount);

    for (int index = 0; index < jointCount; ++index)
    {
        auto *nameLabel =
            new QLabel(
                QStringLiteral("%1[%2]")
                    .arg(groupName)
                    .arg(index),
                groupBox);

        auto *valueLabel =
            new QLabel(
                QStringLiteral("--"),
                groupBox);

        valueLabel->setMinimumWidth(110);

        auto *minusButton =
            new QPushButton(
                QStringLiteral("−"),
                groupBox);

        auto *plusButton =
            new QPushButton(
                QStringLiteral("+"),
                groupBox);

        minusButton->setFixedWidth(44);
        plusButton->setFixedWidth(44);

        connect(
            minusButton,
            &QPushButton::clicked,
            this,
            [this, groupName, index]()
            {
                const double delta =
                    -degreesToRadians(jointStepSpinBox_->value());

                controller_->nudgeJoint(
                    groupName,
                    index,
                    delta,
                    minimumTimeSpinBox_->value());
            });

        connect(
            plusButton,
            &QPushButton::clicked,
            this,
            [this, groupName, index]()
            {
                const double delta =
                    degreesToRadians(jointStepSpinBox_->value());

                controller_->nudgeJoint(
                    groupName,
                    index,
                    delta,
                    minimumTimeSpinBox_->value());
            });

        auto *adjustSlider =
            new QSlider(
                Qt::Horizontal,
                groupBox);

        // Select a relative joint movement from -Bước to +Bước.
        // The command is sent only when the mouse is released.
        const int initialSpan =
            static_cast<int>(jointStepSpinBox_->value() * 100.0);
        adjustSlider->setRange(-initialSpan, initialSpan);
        adjustSlider->setValue(0);
        adjustSlider->setPageStep(100);
        adjustSlider->setTickInterval(3000);
        adjustSlider->setTickPosition(QSlider::TicksBelow);
        adjustSlider->setMinimumWidth(150);
        adjustSlider->setProperty("confirmedDegrees", 0.0);
        adjustSlider->setToolTip(
            QStringLiteral("Kéo và nhả để điều chỉnh từ -Bước đến +Bước."));

        connect(
            adjustSlider,
            &QSlider::sliderReleased,
            this,
            [this, groupName, index, adjustSlider]()
            {
                const int sliderValue = adjustSlider->value();

                const double targetDegrees =
                    static_cast<double>(sliderValue) / 100.0;
                const double confirmedDegrees =
                    adjustSlider->property("confirmedDegrees").toDouble();
                const double deltaDegrees =
                    targetDegrees - confirmedDegrees;

                if (deltaDegrees == 0.0)
                {
                    return;
                }

                controller_->nudgeJoint(
                    groupName,
                    index,
                    degreesToRadians(deltaDegrees),
                    minimumTimeSpinBox_->value());
            });

        // Keep the old controls out of the layout while preserving their
        // existing objects/connections for compatibility with this UI code.
        minusButton->hide();
        plusButton->hide();

        const int row = index + 1;

        layout->addWidget(nameLabel, row, 0);
        layout->addWidget(valueLabel, row, 1);
        layout->addWidget(adjustSlider, row, 2, 1, 2);

        labels.push_back(valueLabel);
        sliders.push_back(adjustSlider);
    }

    // Keep short groups (e.g. Head) aligned to the top when the adjacent
    // group is taller.  The remaining height is reserved below the rows.
    layout->setRowStretch(jointCount + 1, 1);

    jointValueLabels_.insert(
        groupName,
        labels);

    jointSliders_.insert(
        groupName,
        sliders);

    return groupBox;
}

void MainWindow::connectSignals()
{
    connect(
        connectButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if (
                connectionStatusLabel_->text()
                    .contains(QStringLiteral("Đã kết nối")))
            {
                controller_->disconnectFromBridge();
            }
            else
            {
                controller_->connectToBridge();
            }
        });

    connect(
        pingButton_,
        &QPushButton::clicked,
        controller_,
        &RobotController::ping);

    connect(
        logButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            logWindow_->show();
            logWindow_->raise();
            logWindow_->activateWindow();
        });

    connect(
        prepareButton_,
        &QPushButton::clicked,
        controller_,
        &RobotController::prepareRobot);

    connect(
        powerSwitch_,
        &ToggleSwitch::clicked,
        this,
        [this](bool checked)
        {
            if (!checked)
            {
                // Reflect the dependency immediately while the three OFF
                // commands are being acknowledged by the bridge.
                applySystemConfiguration(
                    false,
                    false,
                    false);
            }

            controller_->setPower(checked);
        });

    connect(
        servoSwitch_,
        &ToggleSwitch::clicked,
        this,
        [this](bool checked)
        {
            controller_->setServo(checked);
        });

    connect(
        streamSwitch_,
        &ToggleSwitch::clicked,
        this,
        [this](bool checked)
        {
            controller_->setStream(checked);
        });

    connect(
        cancelButton_,
        &QPushButton::clicked,
        controller_,
        &RobotController::cancelControl);

    connect(
        forwardButton_,
        &QPushButton::pressed,
        this,
        [this]()
        {
            controller_->startDrive(
                0.12,
                0.0,
                0.0);
        });

    connect(
        backwardButton_,
        &QPushButton::pressed,
        this,
        [this]()
        {
            controller_->startDrive(
                -0.12,
                0.0,
                0.0);
        });

    connect(
        leftButton_,
        &QPushButton::pressed,
        this,
        [this]()
        {
            // Follow a left arc while continuing to move forward.
            controller_->startDrive(
                0.10,
                0.0,
                0.30);
        });

    connect(
        rightButton_,
        &QPushButton::pressed,
        this,
        [this]()
        {
            // Follow a right arc while continuing to move forward.
            controller_->startDrive(
                0.10,
                0.0,
                -0.30);
        });

    connect(
        rotateLeftButton_,
        &QPushButton::pressed,
        this,
        [this]()
        {
            controller_->startDrive(
                0.0,
                0.0,
                0.35);
        });

    connect(
        rotateRightButton_,
        &QPushButton::pressed,
        this,
        [this]()
        {
            controller_->startDrive(
                0.0,
                0.0,
                -0.35);
        });

    const QList<QPushButton *> driveButtons{
        forwardButton_,
        backwardButton_,
        leftButton_,
        rightButton_,
        rotateLeftButton_,
        rotateRightButton_
    };

    for (QPushButton *button : driveButtons)
    {
        connect(
            button,
            &QPushButton::released,
            controller_,
            &RobotController::stopDrive);
    }

    connect(
        stopButton_,
        &QPushButton::clicked,
        controller_,
        &RobotController::stopDrive);

    connect(
        armsReadyButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            controller_->sendPose(
                QStringLiteral("arms_ready"),
                QStringLiteral("Co hai tay"),
                minimumTimeSpinBox_->value());
        });

    connect(
        initialButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            controller_->sendPose(
                QStringLiteral("zero_pose"),
                QStringLiteral("Initial"),
                minimumTimeSpinBox_->value());
        });

    connect(
        setReadyButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            controller_->sendPose(
                QStringLiteral("set_ready_pose"),
                QStringLiteral("Set Pose"),
                minimumTimeSpinBox_->value());
        });

    connect(
        goReadyButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            controller_->sendPose(
                QStringLiteral("ready_pose"),
                QStringLiteral("Go Pose"),
                minimumTimeSpinBox_->value());
        });

    connect(
        clearReadyButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            controller_->sendPose(
                QStringLiteral("clear_ready_pose"),
                QStringLiteral("Clear Pose"),
                minimumTimeSpinBox_->value());
        });

    connect(
        controller_,
        &RobotController::stateChanged,
        this,
        &MainWindow::applyControllerState);

    connect(
        controller_,
        &RobotController::systemConfigurationChanged,
        this,
        &MainWindow::applySystemConfiguration);

    connect(
        controller_,
        &RobotController::logMessage,
        this,
        &MainWindow::appendLog);

    connect(
        controller_,
        &RobotController::jointStatusReceived,
        this,
        &MainWindow::updateJointDisplay);

    connect(
        controller_,
        &RobotController::responseReceived,
        this,
        [this](
            const QString &operationName,
            const QJsonObject &response)
        {
            if (operationName == QStringLiteral("Velocity"))
            {
                return;
            }

            if (operationName == QStringLiteral("Đọc trạng thái"))
            {
                updateRobotStatus(response);
                appendLog(compactStatusText(response));
                return;
            }

            const QString prettyJson =
                QString::fromUtf8(
                    QJsonDocument(response)
                        .toJson(QJsonDocument::Indented));

            appendLog(
                QStringLiteral("%1:\n%2")
                    .arg(operationName, prettyJson));
        });
}

void MainWindow::applyControllerState(
    const QString &stateName,
    bool connected,
    bool canDrive,
    bool canControlJoints,
    bool canChangeSystemConfiguration,
    bool busy)
{
    controllerConnected_ = connected;
    canChangeSystemConfiguration_ =
        canChangeSystemConfiguration;
    controllerBusy_ = busy;

    stateLabel_->setText(
        QStringLiteral("State: %1")
            .arg(stateName));

    robotControllerStateValueLabel_->setText(stateName);
    robotReadyValueLabel_->setText(
        stateName == QStringLiteral("Ready")
            ? QStringLiteral("Có")
            : QStringLiteral("Không"));
    robotReadyValueLabel_->setStyleSheet(
        stateName == QStringLiteral("Ready")
            ? QStringLiteral("color:#167a35;font-weight:600;")
            : QStringLiteral("color:#666666;font-weight:600;"));

    robotConnectionValueLabel_->setText(
        connected
            ? QStringLiteral("Đã kết nối")
            : QStringLiteral("Chưa kết nối"));
    robotConnectionValueLabel_->setStyleSheet(
        connected
            ? QStringLiteral("color:#167a35;font-weight:600;")
            : QStringLiteral("color:#a32121;font-weight:600;"));

    if (!connected)
    {
        robotBridgeStateValueLabel_->setText(QStringLiteral("—"));
        robotLastUpdateValueLabel_->setText(QStringLiteral("—"));
        robotMessageValueLabel_->setText(QStringLiteral("—"));
    }

    connectionStatusLabel_->setText(
        connected
            ? QStringLiteral("Kết nối: Đã kết nối bridge")
            : QStringLiteral("Kết nối: Chưa kết nối"));

    connectButton_->setText(
        connected
            ? QStringLiteral("Ngắt kết nối")
            : QStringLiteral("Kết nối"));

    pingButton_->setEnabled(connected);

    updateSystemSwitchAvailability();

    cancelButton_->setEnabled(connected);

    driveGroup_->setEnabled(
        connected && canDrive && !busy);

    const bool robotReady =
        connected
        && stateName == QStringLiteral("Ready")
        && canControlJoints
        && !busy;

    upperBodyContent_->setEnabled(robotReady);

    tabWidget_->setEnabled(connected);
}

void MainWindow::applySystemConfiguration(
    bool powerEnabled,
    bool servoEnabled,
    bool streamEnabled)
{
    powerEnabled_ = powerEnabled;

    powerSwitch_->blockSignals(true);
    servoSwitch_->blockSignals(true);
    streamSwitch_->blockSignals(true);

    powerSwitch_->setChecked(powerEnabled);
    servoSwitch_->setChecked(
        powerEnabled && servoEnabled);
    streamSwitch_->setChecked(
        powerEnabled && streamEnabled);

    powerSwitch_->blockSignals(false);
    servoSwitch_->blockSignals(false);
    streamSwitch_->blockSignals(false);

    robotPowerValueLabel_->setText(
        powerEnabled ? QStringLiteral("Bật") : QStringLiteral("Tắt"));
    robotServoValueLabel_->setText(
        powerEnabled && servoEnabled
            ? QStringLiteral("Bật")
            : QStringLiteral("Tắt"));
    robotStreamValueLabel_->setText(
        powerEnabled && streamEnabled
            ? QStringLiteral("Bật")
            : QStringLiteral("Tắt"));

    updateSystemSwitchAvailability();
}

void MainWindow::updateSystemSwitchAvailability()
{
    const bool configurationAvailable =
        controllerConnected_
        && canChangeSystemConfiguration_
        && !controllerBusy_;

    prepareButton_->setEnabled(
        configurationAvailable);

    powerSwitch_->setEnabled(configurationAvailable);
    servoSwitch_->setEnabled(
        configurationAvailable && powerEnabled_);
    streamSwitch_->setEnabled(
        configurationAvailable && powerEnabled_);
}

void MainWindow::appendLog(
    const QString &message)
{
    const QString time =
        QDateTime::currentDateTime()
            .toString(QStringLiteral("HH:mm:ss"));

    logTextEdit_->append(
        QStringLiteral("[%1] %2")
            .arg(time, message));
}

void MainWindow::updateRobotStatus(
    const QJsonObject &response)
{
    const QJsonObject status =
        response.value(QStringLiteral("status")).toObject();

    const auto setTextValue =
        [&response, &status](
            QLabel *label,
            const QStringList &keys)
        {
            const QJsonValue value =
                findStatusValue(response, status, keys);

            label->setText(
                value.isUndefined()
                    ? QStringLiteral("—")
                    : statusValueText(value));
        };

    const auto setBooleanValue =
        [&response, &status](
            QLabel *label,
            const QStringList &keys,
            const QString &enabledText,
            const QString &disabledText)
        {
            const QJsonValue value =
                findStatusValue(response, status, keys);

            if (!value.isBool())
            {
                label->setText(QStringLiteral("—"));
                return;
            }

            label->setText(
                value.toBool() ? enabledText : disabledText);
        };

    setTextValue(
        robotBridgeStateValueLabel_,
        {
            QStringLiteral("state"),
            QStringLiteral("robot_state"),
            QStringLiteral("control_state")
        });

    setBooleanValue(
        robotPowerValueLabel_,
        {
            QStringLiteral("power"),
            QStringLiteral("power_on"),
            QStringLiteral("powered")
        },
        QStringLiteral("Bật"),
        QStringLiteral("Tắt"));

    setBooleanValue(
        robotServoValueLabel_,
        {
            QStringLiteral("servo"),
            QStringLiteral("servo_on"),
            QStringLiteral("servo_enabled")
        },
        QStringLiteral("Bật"),
        QStringLiteral("Tắt"));

    setBooleanValue(
        robotStreamValueLabel_,
        {
            QStringLiteral("stream"),
            QStringLiteral("stream_on"),
            QStringLiteral("streaming"),
            QStringLiteral("stream_enabled")
        },
        QStringLiteral("Bật"),
        QStringLiteral("Tắt"));

    setTextValue(
        robotMessageValueLabel_,
        {
            QStringLiteral("message"),
            QStringLiteral("error")
        });

    robotLastUpdateValueLabel_->setText(
        QDateTime::currentDateTime()
            .toString(QStringLiteral("HH:mm:ss")));
}

void MainWindow::updateJointDisplay(
    const QJsonObject &response)
{
    QJsonObject groupsObject;

    if (
        response.contains(
            QStringLiteral("groups"))
        && response.value(
            QStringLiteral("groups")).isObject())
    {
        groupsObject =
            response.value(
                QStringLiteral("groups")).toObject();
    }
    else
    {
        groupsObject = response;
    }

    const QStringList groupNames{
        QStringLiteral("torso"),
        QStringLiteral("head"),
        QStringLiteral("right_arm"),
        QStringLiteral("left_arm")
    };

    for (const QString &groupName : groupNames)
    {
        if (!groupsObject.contains(groupName))
        {
            continue;
        }

        QJsonArray positions;

        const QJsonValue groupValue =
            groupsObject.value(groupName);

        if (groupValue.isArray())
        {
            positions = groupValue.toArray();
        }
        else if (groupValue.isObject())
        {
            const QJsonObject groupObject =
                groupValue.toObject();

            if (
                groupObject.contains(
                    QStringLiteral("positions"))
                && groupObject.value(
                    QStringLiteral("positions")).isArray())
            {
                positions =
                    groupObject.value(
                        QStringLiteral("positions")).toArray();
            }
        }

        if (positions.isEmpty())
        {
            continue;
        }

        auto it =
            jointValueLabels_.find(groupName);

        if (it == jointValueLabels_.end())
        {
            continue;
        }

        QVector<QLabel *> &labels = it.value();

        auto sliderIt =
            jointSliders_.find(groupName);

        if (sliderIt == jointSliders_.end())
        {
            continue;
        }

        QVector<QSlider *> &sliders = sliderIt.value();

        const int count =
            qMin(
                static_cast<int>(positions.size()),
                qMin(
                    static_cast<int>(labels.size()),
                    static_cast<int>(sliders.size())));

        for (int index = 0; index < count; ++index)
        {
            const double degrees =
                radiansToDegrees(
                    positions.at(index).toDouble());

            labels[index]->setText(
                QStringLiteral("%1 \u00B0")
                    .arg(
                        degrees,
                        0,
                        'f',
                        2));

            sliders[index]->setProperty(
                "confirmedDegrees",
                degrees);

            const int center =
                static_cast<int>(degrees * 100.0);
            const int span =
                static_cast<int>(jointStepSpinBox_->value() * 100.0);
            sliders[index]->setRange(center - span, center + span);
            sliders[index]->setValue(center);
        }
    }
}

void MainWindow::closeEvent(
    QCloseEvent *event)
{
    logWindow_->close();
    controller_->stopDrive();
    controller_->disconnectFromBridge();

    QMainWindow::closeEvent(event);
}
