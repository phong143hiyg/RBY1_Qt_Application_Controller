#include "ui/MainWindow.hpp"
#include "ui/ToggleSwitch.hpp"

#include "controller/RobotController.hpp"

#include <QCloseEvent>
#include <QDateTime>
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
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

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

    statusButton_ =
        new QPushButton(
            QStringLiteral("Đọc trạng thái"),
            connectionGroup);

    connectionLayout->addWidget(connectButton_);
    connectionLayout->addWidget(pingButton_);
    connectionLayout->addWidget(statusButton_);
    connectionLayout->addStretch();

    tabWidget_ = new QTabWidget(centralWidget);

    tabWidget_->addTab(
        buildSystemAndBaseTab(),
        QStringLiteral("Hệ thống & đế"));

    tabWidget_->addTab(
        buildUpperBodyTab(),
        QStringLiteral("Thân, đầu & hai tay"));

    logTextEdit_ = new QTextEdit(centralWidget);
    logTextEdit_->setReadOnly(true);
    logTextEdit_->setMaximumHeight(240);

    QFont logFont(QStringLiteral("Consolas"));
    logFont.setStyleHint(QFont::Monospace);
    logTextEdit_->setFont(logFont);

    mainLayout->addWidget(titleLabel);
    mainLayout->addLayout(statusLayout);
    mainLayout->addWidget(connectionGroup);
    mainLayout->addWidget(tabWidget_, 1);
    mainLayout->addWidget(logTextEdit_);

    setCentralWidget(centralWidget);
}

QWidget *MainWindow::buildSystemAndBaseTab()
{
    auto *tab = new QWidget();
    auto *layout = new QHBoxLayout(tab);

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
            QStringLiteral("← Sang trái"),
            driveGroup_);

    stopButton_ =
        new QPushButton(
            QStringLiteral("DỪNG"),
            driveGroup_);

    rightButton_ =
        new QPushButton(
            QStringLiteral("Sang phải →"),
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
        button->setMinimumSize(140, 58);
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

    layout->addWidget(systemGroup_, 1);
    layout->addWidget(driveGroup_, 2);

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

    refreshJointsButton_ =
        new QPushButton(
            QStringLiteral("Làm mới khớp"),
            toolbarGroup);

    armsReadyButton_ =
        new QPushButton(
            QStringLiteral("Co hai tay"),
            toolbarGroup);

    readyPoseButton_ =
        new QPushButton(
            QStringLiteral("Ready pose"),
            toolbarGroup);

    zeroPoseButton_ =
        new QPushButton(
            QStringLiteral("Zero pose"),
            toolbarGroup);

    jointStepSpinBox_ =
        new QDoubleSpinBox(toolbarGroup);

    jointStepSpinBox_->setRange(0.01, 0.20);
    jointStepSpinBox_->setDecimals(2);
    jointStepSpinBox_->setSingleStep(0.01);
    jointStepSpinBox_->setValue(0.05);
    jointStepSpinBox_->setSuffix(
        QStringLiteral(" rad"));

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

    toolbarLayout->addWidget(
        refreshJointsButton_,
        1,
        0);

    toolbarLayout->addWidget(
        armsReadyButton_,
        1,
        1);

    toolbarLayout->addWidget(
        readyPoseButton_,
        1,
        2);

    toolbarLayout->addWidget(
        zeroPoseButton_,
        1,
        3);

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
                    -jointStepSpinBox_->value();

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
                    jointStepSpinBox_->value();

                controller_->nudgeJoint(
                    groupName,
                    index,
                    delta,
                    minimumTimeSpinBox_->value());
            });

        const int row = index + 1;

        layout->addWidget(nameLabel, row, 0);
        layout->addWidget(valueLabel, row, 1);
        layout->addWidget(minusButton, row, 2);
        layout->addWidget(plusButton, row, 3);

        labels.push_back(valueLabel);
    }

    jointValueLabels_.insert(
        groupName,
        labels);

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
        statusButton_,
        &QPushButton::clicked,
        controller_,
        &RobotController::requestStatus);

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
            controller_->startDrive(
                0.0,
                0.10,
                0.0);
        });

    connect(
        rightButton_,
        &QPushButton::pressed,
        this,
        [this]()
        {
            controller_->startDrive(
                0.0,
                -0.10,
                0.0);
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
        refreshJointsButton_,
        &QPushButton::clicked,
        controller_,
        &RobotController::refreshJoints);

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
        readyPoseButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            controller_->sendPose(
                QStringLiteral("ready_pose"),
                QStringLiteral("Ready pose"),
                minimumTimeSpinBox_->value());
        });

    connect(
        zeroPoseButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            controller_->sendPose(
                QStringLiteral("zero_pose"),
                QStringLiteral("Zero pose"),
                minimumTimeSpinBox_->value());
        });

    connect(
        controller_,
        &RobotController::stateChanged,
        this,
        &MainWindow::applyControllerState);

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
    bool busy)
{
    stateLabel_->setText(
        QStringLiteral("State: %1")
            .arg(stateName));

    connectionStatusLabel_->setText(
        connected
            ? QStringLiteral("Kết nối: Đã kết nối bridge")
            : QStringLiteral("Kết nối: Chưa kết nối"));

    connectButton_->setText(
        connected
            ? QStringLiteral("Ngắt kết nối")
            : QStringLiteral("Kết nối"));

    pingButton_->setEnabled(connected);
    statusButton_->setEnabled(connected);

    prepareButton_->setEnabled(
        connected && !busy);

    powerSwitch_->setEnabled(
        connected && !busy);

    servoSwitch_->setEnabled(
        connected && !busy);

    streamSwitch_->setEnabled(
        connected && !busy);

    if (canDrive) {
        powerSwitch_->blockSignals(true);
        servoSwitch_->blockSignals(true);
        streamSwitch_->blockSignals(true);

        powerSwitch_->setChecked(true);
        servoSwitch_->setChecked(true);
        streamSwitch_->setChecked(true);

        powerSwitch_->blockSignals(false);
        servoSwitch_->blockSignals(false);
        streamSwitch_->blockSignals(false);
    } else if (!connected) {
        powerSwitch_->blockSignals(true);
        servoSwitch_->blockSignals(true);
        streamSwitch_->blockSignals(true);

        powerSwitch_->setChecked(false);
        servoSwitch_->setChecked(false);
        streamSwitch_->setChecked(false);

        powerSwitch_->blockSignals(false);
        servoSwitch_->blockSignals(false);
        streamSwitch_->blockSignals(false);
    }

    cancelButton_->setEnabled(connected);

    driveGroup_->setEnabled(
        connected && canDrive && !busy);

    upperBodyContent_->setEnabled(
        connected && canControlJoints && !busy);

    tabWidget_->setEnabled(connected);
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

        const int count =
            qMin(
                static_cast<int>(positions.size()),
                static_cast<int>(labels.size()));

        for (int index = 0; index < count; ++index)
        {
            labels[index]->setText(
                QStringLiteral("%1 rad")
                    .arg(
                        positions.at(index).toDouble(),
                        0,
                        'f',
                        3));
        }
    }
}

void MainWindow::closeEvent(
    QCloseEvent *event)
{
    controller_->stopDrive();
    controller_->disconnectFromBridge();

    QMainWindow::closeEvent(event);
}
