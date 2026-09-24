#include "ui/MainWindow.hpp"
#include "ui/ToggleSwitch.hpp"
#include "ui/JointAngleEdit.hpp"
#include "ui/PlanningPanel.hpp"

#include "controller/RobotController.hpp"

#include <QCloseEvent>
#include <QComboBox>
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
#include <QLineEdit>
#include <QMessageBox>
#include <QList>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QTabWidget>
#include <QTextDocument>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>
#include <QUrl>
#include <QWidget>
#include <QtMath>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kManualJointLimitMarginDegrees = 1.0;

double degreesToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

double radiansToDegrees(double radians)
{
    return radians * 180.0 / kPi;
}

struct JointLimits
{
    double minimumDegrees;
    double maximumDegrees;
};

JointLimits jointLimits(
    const QString &groupName,
    int jointIndex)
{
    if (groupName == QStringLiteral("torso"))
    {
        switch (jointIndex)
        {
        case 0: return {-15.0, 15.0};
        case 1: return {-30.0, 90.0};
        case 2: return {-150.0, 90.0};
        case 3: return {-45.0, 90.0};
        case 4: return {-30.0, 30.0};
        case 5: return {-135.0, 135.0};
        default: break;
        }
    }

    if (groupName == QStringLiteral("head"))
    {
        return {-180.0, 180.0};
    }

    if (groupName == QStringLiteral("right_arm"))
    {
        switch (jointIndex)
        {
        case 0: return {-180.0, 180.0};
        case 1: return {-180.0, 0.0};
        case 2: return {-180.0, 180.0};
        case 3: return {-150.0, 0.0};
        case 4: return {-180.0, 180.0};
        case 5: return {-90.0, 110.0};
        case 6: return {-155.0, 155.0};
        default: break;
        }
    }

    if (groupName == QStringLiteral("left_arm"))
    {
        switch (jointIndex)
        {
        case 0: return {-180.0, 180.0};
        case 1: return {0.0, 180.0};
        case 2: return {-180.0, 180.0};
        case 3: return {-150.0, 0.0};
        case 4: return {-180.0, 180.0};
        case 5: return {-90.0, 110.0};
        case 6: return {-155.0, 155.0};
        default: break;
        }
    }

    // Safe fallback for an unknown joint.
    return {-180.0, 180.0};
}

JointLimits manualJointLimits(
    const QString &groupName,
    int jointIndex)
{
    const JointLimits hardwareLimits =
        jointLimits(groupName, jointIndex);

    // Manual controls stay slightly inside the mechanical endpoints. A
    // command at the exact endpoint can make the robot reject control and
    // terminate the active command stream.
    return {
        hardwareLimits.minimumDegrees
            + kManualJointLimitMarginDegrees,
        hardwareLimits.maximumDegrees
            - kManualJointLimitMarginDegrees
    };
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
    const ParsedSystemStatus parsed = parseSystemStatus(response);
    if (parsed.accepted)
    {
        const auto appendComponent =
            [&parts](const QString &name, const ComponentStatus &component)
            {
                parts.append(
                    QStringLiteral("%1=%2%3")
                        .arg(
                            name,
                            componentStateText(component.state),
                            component.legacy
                                ? QStringLiteral("[legacy]")
                                : QString{}));
            };

        appendComponent(QStringLiteral("power"), parsed.power);
        appendComponent(QStringLiteral("servo"), parsed.servo);
        appendComponent(QStringLiteral("stream"), parsed.stream);
    }
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
    applySystemConfiguration({});
}

void MainWindow::buildInterface()
{
    setWindowTitle(
        QStringLiteral(
            "RBY1 Desktop Controller - State Pattern"));

    resize(1200, 840);

    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(centralWidget);

    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    auto *titleLabel =
        new QLabel(
            QStringLiteral(
                "RBY1 Desktop Controller"),
            centralWidget);

    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(15);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(6);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    robotAddressEdit_ = new QLineEdit(centralWidget);
    robotAddressEdit_->setObjectName(QStringLiteral("robotAddressEdit"));
    robotAddressEdit_->setText(qEnvironmentVariable("RBY1_ROBOT_ADDRESS", QStringLiteral("127.0.0.1:55051")));
    robotAddressEdit_->setToolTip(QStringLiteral(
        "Simulator RBY1-M trên máy này dùng 127.0.0.1:55051."));
    headerLayout->addWidget(new QLabel(QStringLiteral("Robot:"), centralWidget));
    robotAddressEdit_->setMaximumWidth(240);
    headerLayout->addWidget(robotAddressEdit_);

    robotModelComboBox_ = new QComboBox(centralWidget);
    robotModelComboBox_->setObjectName(QStringLiteral("robotModelComboBox"));
    robotModelComboBox_->addItem(QStringLiteral("RBY1-M"), static_cast<int>(Rby1Model::M));
    robotModelComboBox_->setCurrentIndex(0);
    robotModelComboBox_->setEnabled(false);
    robotModelComboBox_->setToolTip(QStringLiteral("Ứng dụng được cấu hình cố định cho RBY1-M."));
    headerLayout->addWidget(robotModelComboBox_);

    connectButton_ =
        new QPushButton(
            QStringLiteral("Kết nối"),
            centralWidget);

    pingButton_ =
        new QPushButton(
            QStringLiteral("Ping"),
            centralWidget);

    logButton_ =
        new QPushButton(
            QStringLiteral("Log"),
            centralWidget);

    headerLayout->addWidget(connectButton_);
    headerLayout->addWidget(pingButton_);
    headerLayout->addWidget(logButton_);

    tabWidget_ = new QTabWidget(centralWidget);
    tabWidget_->setTabBarAutoHide(true);

    tabWidget_->addTab(
        buildSystemAndBaseTab(),
        QStringLiteral("Điều khiển robot"));
    tabWidget_->addTab(new PlanningPanel(tabWidget_), QStringLiteral("Test quỹ đạo"));

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

    mainLayout->addLayout(headerLayout);
    mainLayout->addWidget(tabWidget_, 1);

    setCentralWidget(centralWidget);
}

QWidget *MainWindow::buildSystemAndBaseTab()
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    auto *topLayout = new QHBoxLayout();
    topLayout->setSpacing(6);

    systemGroup_ =
        new QGroupBox(
            QStringLiteral(
                "Nguồn, servo và Control Manager"),
            tab);

    auto *systemLayout =
        new QGridLayout(systemGroup_);
    systemLayout->setContentsMargins(8, 18, 8, 6);
    systemLayout->setSpacing(4);

    prepareButton_ =
        new QPushButton(
            QStringLiteral("CHUẨN BỊ ROBOT"),
            systemGroup_);

    prepareButton_->setMinimumHeight(28);
    prepareButton_->setToolTip(
        QStringLiteral(
            "Bật nhanh Power, Servo và Control Manager rồi đưa robot vào Ready."));

    powerSwitch_ =
        new ToggleSwitch(
            QStringLiteral("Power"),
            systemGroup_);
    powerSwitch_->setObjectName(QStringLiteral("powerSwitch"));

    servoSwitch_ =
        new ToggleSwitch(
            QStringLiteral("Servo"),
            systemGroup_);
    servoSwitch_->setObjectName(QStringLiteral("servoSwitch"));

    streamSwitch_ =
        new ToggleSwitch(
            QStringLiteral("Control Manager"),
            systemGroup_);
    streamSwitch_->setObjectName(QStringLiteral("streamSwitch"));

    systemLayout->addWidget(
        prepareButton_, 0, 0, 1, 2);

    systemLayout->addWidget(
        powerSwitch_, 1, 0, 1, 2);

    systemLayout->addWidget(
        servoSwitch_, 2, 0, 1, 2);

    systemLayout->addWidget(
        streamSwitch_, 3, 0, 1, 2);

    driveGroup_ =
        new QGroupBox(
            QStringLiteral(
                "Điều khiển đế robot"),
            tab);

    auto *driveLayout =
        new QGridLayout(driveGroup_);
    driveLayout->setContentsMargins(8, 18, 8, 6);
    driveLayout->setSpacing(4);

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
        button->setMinimumSize(86, 32);
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
    robotStatusLayout->setContentsMargins(8, 18, 8, 6);
    robotStatusLayout->setHorizontalSpacing(8);
    robotStatusLayout->setVerticalSpacing(3);

    robotConnectionValueLabel_ = new QLabel(QStringLiteral("Chưa kết nối"), robotStatusGroup_);
    robotControllerStateValueLabel_ = new QLabel(QStringLiteral("Disconnected"), robotStatusGroup_);
    robotReportedStateValueLabel_ = new QLabel(QStringLiteral("—"), robotStatusGroup_);
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
        {QStringLiteral("State robot:"), robotReportedStateValueLabel_},
        {QStringLiteral("Ready:"), robotReadyValueLabel_},
        {QStringLiteral("Power:"), robotPowerValueLabel_},
        {QStringLiteral("Servo:"), robotServoValueLabel_},
        {QStringLiteral("Control Manager:"), robotStreamValueLabel_},
        {QStringLiteral("Cập nhật lúc:"), robotLastUpdateValueLabel_},
        {QStringLiteral("Thông báo:"), robotMessageValueLabel_}
    };

    for (int row = 0; row < statusRows.size(); ++row)
    {
        auto *nameLabel =
            new QLabel(statusRows.at(row).first, robotStatusGroup_);
        nameLabel->setStyleSheet(
            QStringLiteral("font-weight:600;"));

        const int gridRow = row / 2;
        const int gridColumn = (row % 2) * 2;
        robotStatusLayout->addWidget(nameLabel, gridRow, gridColumn);
        robotStatusLayout->addWidget(statusRows.at(row).second, gridRow, gridColumn + 1);
    }

    robotStatusLayout->setColumnStretch(1, 1);
    robotStatusLayout->setColumnStretch(3, 1);

    for (QGroupBox *group : {systemGroup_, driveGroup_, robotStatusGroup_})
    {
        group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    }

    topLayout->addWidget(systemGroup_, 1, Qt::AlignTop);
    topLayout->addWidget(driveGroup_, 2, Qt::AlignTop);
    topLayout->addWidget(robotStatusGroup_, 2, Qt::AlignTop);

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
    tabLayout->setContentsMargins(0, 0, 0, 0);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *toolbarGroup =
        new QGroupBox(
            QStringLiteral(
                "Điều khiển thân trên"),
            upperBodyContent_);

    auto *toolbarLayout = new QHBoxLayout(toolbarGroup);
    toolbarLayout->setContentsMargins(8, 18, 8, 6);
    toolbarLayout->setSpacing(6);

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
    armsReadyButton_->setToolTip(
        QStringLiteral("Co hai tay về tư thế Ready chuẩn của RBY1."));
    setReadyButton_->setToolTip(
        QStringLiteral("Lưu toàn bộ vị trí khớp hiện tại làm pose."));
    goReadyButton_->setToolTip(
        QStringLiteral("Đưa robot về pose đã lưu."));
    clearReadyButton_->setToolTip(
        QStringLiteral("Xóa pose đã lưu."));

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
            QStringLiteral("Thời gian:"),
            toolbarGroup));
    minimumTimeSpinBox_->setMaximumWidth(96);
    toolbarLayout->addWidget(minimumTimeSpinBox_);
    toolbarLayout->addWidget(initialButton_, 1);
    toolbarLayout->addWidget(armsReadyButton_, 1);
    toolbarLayout->addWidget(setReadyButton_, 1);
    toolbarLayout->addWidget(goReadyButton_, 1);
    toolbarLayout->addWidget(clearReadyButton_, 1);

    auto *scrollArea =
        new QScrollArea(upperBodyContent_);
    scrollArea->setObjectName(QStringLiteral("jointScrollArea"));
    scrollArea->setFrameShape(QFrame::NoFrame);

    scrollArea->setWidgetResizable(true);

    auto *scrollContent =
        new QWidget(scrollArea);

    auto *jointGrid = new QGridLayout(scrollContent);
    jointGrid->setContentsMargins(0, 0, 0, 0);
    jointGrid->setSpacing(6);
    jointGrid->setColumnStretch(0, 1);
    jointGrid->setColumnStretch(1, 1);
    // Extra viewport space goes below the two aligned rows, not between them.
    jointGrid->setRowStretch(2, 1);

    jointGrid->addWidget(
        buildJointGroup(
            QStringLiteral("Torso"),
            QStringLiteral("torso"),
            6), 0, 0);

    jointGrid->addWidget(
        buildJointGroup(
            QStringLiteral("Head"),
            QStringLiteral("head"),
            2), 0, 1);

    jointGrid->addWidget(
        buildJointGroup(
            QStringLiteral("Right Arm"),
            QStringLiteral("right_arm"),
            7), 1, 0);

    jointGrid->addWidget(
        buildJointGroup(
            QStringLiteral("Left Arm"),
            QStringLiteral("left_arm"),
            7), 1, 1);

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
    groupBox->setObjectName(QStringLiteral("%1JointGroup").arg(groupName));
    auto *layout = new QGridLayout(groupBox);
    layout->setContentsMargins(8, 18, 8, 6);
    layout->setHorizontalSpacing(4);
    layout->setVerticalSpacing(3);
    groupBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    // Col 5 (slider) stretches; all other columns are fixed.
    layout->setColumnStretch(5, 1);

    QVector<JointAngleEdit *> labels;
    labels.reserve(jointCount);

    QVector<QSlider *> sliders;
    sliders.reserve(jointCount);

    for (int index = 0; index < jointCount; ++index)
    {
        const JointLimits limits = manualJointLimits(groupName, index);

        // Col 0: θN: symbol label
        auto *nameLabel =
            new QLabel(
                QStringLiteral("\u03B8%1:").arg(index + 1),
                groupBox);
        nameLabel->setFixedWidth(34);
        nameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // Col 1: current value — cyan background, bold, right-aligned
        auto *valueLabel = new JointAngleEdit(groupBox);
        valueLabel->setObjectName(
            QStringLiteral("%1Joint%2Value").arg(groupName).arg(index));
        valueLabel->setFixedWidth(92);
        valueLabel->setMinimumHeight(26);
        QFont valueFont = valueLabel->font();
        valueFont.setPointSize(11);
        valueFont.setBold(true);
        valueLabel->setFont(valueFont);
        valueLabel->setToolTip(QStringLiteral(
            "Nhập góc theo độ rồi nhấn Enter để di chuyển. "
            "Bấm ra ngoài để bỏ giá trị chưa gửi."));
        valueLabel->setStyleSheet(
            QStringLiteral(
                "background-color:#00c8ff;"
                "color:#000000;"
                "font-weight:bold;"
                "padding:2px 5px;"
                "border:1px solid #0099cc;"));

        // Col 2: degree unit label
        auto *unitLabel =
            new QLabel(
                QStringLiteral("\u00B0"),
                groupBox);
        unitLabel->setFixedWidth(14);

        // Col 3: minimum limit label
        auto *minLabel =
            new QLabel(
                QString::number(limits.minimumDegrees, 'f', 1),
                groupBox);
        minLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        minLabel->setStyleSheet(
            QStringLiteral("color:#555555;font-size:9pt;"));

        // Col 4: ◄ nudge-left button
        auto *minusButton =
            new QPushButton(
                QStringLiteral("\u25C4"),
                groupBox);
        minusButton->setFixedSize(22, 22);
        minusButton->setObjectName(QStringLiteral("%1Joint%2Minus").arg(groupName).arg(index));
        minusButton->setToolTip(
            QStringLiteral("Giảm 1°"));

        // Col 5: position slider (stretches)
        auto *adjustSlider =
            new QSlider(Qt::Horizontal, groupBox);
        adjustSlider->setObjectName(
            QStringLiteral("%1Joint%2Slider").arg(groupName).arg(index));
        adjustSlider->setRange(
            static_cast<int>(limits.minimumDegrees * 100.0),
            static_cast<int>(limits.maximumDegrees * 100.0));
        adjustSlider->setValue(0);
        adjustSlider->setPageStep(100);
        adjustSlider->setTickInterval(3000);
        adjustSlider->setTickPosition(QSlider::TicksBelow);
        adjustSlider->setMinimumWidth(120);
        adjustSlider->setProperty("confirmedDegrees", 0.0);
        adjustSlider->setToolTip(
            QStringLiteral(
                "Kéo và nhả để đặt vị trí khớp."));

        // Col 6: ► nudge-right button
        auto *plusButton =
            new QPushButton(
                QStringLiteral("\u25BA"),
                groupBox);
        plusButton->setFixedSize(22, 22);
        plusButton->setObjectName(QStringLiteral("%1Joint%2Plus").arg(groupName).arg(index));
        plusButton->setToolTip(
            QStringLiteral("Tăng 1°"));

        // Col 7: maximum limit label
        auto *maxLabel =
            new QLabel(
                QString::number(limits.maximumDegrees, 'f', 1),
                groupBox);
        maxLabel->setStyleSheet(
            QStringLiteral("color:#555555;font-size:9pt;"));

        // ---- Connections ----

        connect(valueLabel, &QLineEdit::returnPressed, this,
            [this, groupName, index, valueLabel]()
            {
                bool valid = false;
                const double targetDegrees = valueLabel->text().trimmed().toDouble(&valid);
                if (!valid || !qIsFinite(targetDegrees))
                {
                    showJointMotionError(QStringLiteral("Góc khớp phải là một số hữu hạn, tính theo độ."));
                    return;
                }
                // Return to live display before sending; never display the
                // entered target as though the robot already reached it.
                valueLabel->clearFocus();
                submitJointTarget(groupName, index, targetDegrees);
            });

        connect(
            minusButton,
            &QPushButton::clicked,
            this,
            [this, groupName, index]()
            {
                submitJointStep(groupName, index, -1.0);
            });

        connect(
            plusButton,
            &QPushButton::clicked,
            this,
            [this, groupName, index]()
            {
                submitJointStep(groupName, index, 1.0);
            });

        connect(
            adjustSlider,
            &QSlider::sliderReleased,
            this,
            [this, groupName, index, adjustSlider]()
            {
                const int sliderValue = adjustSlider->value();

                const double targetDegrees =
                    static_cast<double>(sliderValue) / 100.0;
                submitJointTarget(groupName, index, targetDegrees);
            });

        // ---- Layout ----

        const int row = index;

        layout->addWidget(nameLabel,    row, 0);
        layout->addWidget(valueLabel,   row, 1);
        layout->addWidget(unitLabel,    row, 2);
        layout->addWidget(minLabel,     row, 3);
        layout->addWidget(minusButton,  row, 4);
        layout->addWidget(adjustSlider, row, 5);
        layout->addWidget(plusButton,   row, 6);
        layout->addWidget(maxLabel,     row, 7);

        labels.push_back(valueLabel);
        sliders.push_back(adjustSlider);
    }

    // Keep corresponding joint rows aligned at the top, even for Head,
    // whose symmetric box has the same height as the six-joint Torso box.
    layout->setRowStretch(jointCount, 1);

    jointValueLabels_.insert(groupName, labels);
    jointConfirmedDegrees_.insert(groupName, QVector<double>(jointCount, qQNaN()));
    jointSliders_.insert(groupName, sliders);

    return groupBox;
}

void MainWindow::connectSignals()
{
    connect(controller_, &RobotController::jointMotionFailed,
        this, &MainWindow::showJointMotionError);
    connect(
        connectButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if (controllerConnected_)
            {
                controller_->disconnectFromRobot();
            }
            else
            {
                const QUrl address(QStringLiteral("tcp://") + robotAddressEdit_->text().trimmed());
                if (address.host().isEmpty() || address.port() < 1 || address.port() > 65535
                    || !address.path().isEmpty() || !address.userInfo().isEmpty()
                    || address.hasQuery() || address.hasFragment())
                {
                    QMessageBox::warning(this, QStringLiteral("Địa chỉ không hợp lệ"),
                        QStringLiteral("Nhập địa chỉ dạng IP:port, ví dụ 127.0.0.1:50051."));
                    return;
                }
                const auto model = static_cast<Rby1Model>(robotModelComboBox_->currentData().toInt());
                controller_->connectToRobot(address.host(), static_cast<quint16>(address.port()), model);
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
        [this](bool)
        {
            controller_->togglePower();
        });

    connect(
        servoSwitch_,
        &ToggleSwitch::clicked,
        this,
        [this](bool)
        {
            controller_->toggleServo();
        });

    connect(
        streamSwitch_,
        &ToggleSwitch::clicked,
        this,
        [this](bool)
        {
            controller_->toggleStream();
        });

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

            // Joint snapshots arrive continuously; do not flood the log.
            if (operationName == QStringLiteral("Joints status"))
            {
                return;
            }

            if (operationName == QStringLiteral("Status"))
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
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(
            this,
            [this,
             stateName,
             connected,
             canDrive,
             canControlJoints,
             canChangeSystemConfiguration,
             busy]()
            {
                applyControllerState(
                    stateName,
                    connected,
                    canDrive,
                    canControlJoints,
                    canChangeSystemConfiguration,
                    busy);
            },
            Qt::QueuedConnection);
        return;
    }

    controllerConnected_ = connected;
    canChangeSystemConfiguration_ =
        canChangeSystemConfiguration;
    controllerBusy_ = busy;

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
        for (auto it = jointConfirmedDegrees_.begin(); it != jointConfirmedDegrees_.end(); ++it)
        {
            it->fill(qQNaN());
        }
        for (const auto &editors : jointValueLabels_)
        {
            for (JointAngleEdit *editor : editors)
            {
                editor->clearConfirmedDegrees();
            }
        }
        robotReportedStateValueLabel_->setText(QStringLiteral("—"));
        robotLastUpdateValueLabel_->setText(QStringLiteral("—"));
        robotMessageValueLabel_->setText(QStringLiteral("—"));
    }

    connectButton_->setText(
        connected
            ? QStringLiteral("Ngắt kết nối")
            : QStringLiteral("Kết nối"));

    pingButton_->setEnabled(connected);
    robotAddressEdit_->setEnabled(!connected);
    robotModelComboBox_->setEnabled(false);

    updateSystemSwitchAvailability();

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
    const SystemConfigurationView &configuration)
{
    if (QThread::currentThread() != thread())
    {
        const SystemConfigurationView copy = configuration;
        QMetaObject::invokeMethod(
            this,
            [this, copy]() { applySystemConfiguration(copy); },
            Qt::QueuedConnection);
        return;
    }

    systemConfiguration_ = configuration;

    applyComponentView(
        powerSwitch_,
        robotPowerValueLabel_,
        QStringLiteral("Power"),
        configuration.power);
    applyComponentView(
        servoSwitch_,
        robotServoValueLabel_,
        QStringLiteral("Servo"),
        configuration.servo);
    applyComponentView(
        streamSwitch_,
        robotStreamValueLabel_,
        QStringLiteral("Control Manager"),
        configuration.stream);

    updateSystemSwitchAvailability();
}

void MainWindow::applyComponentView(
    ToggleSwitch *toggle,
    QLabel *statusLabel,
    const QString &name,
    const ComponentViewState &component)
{
    const QString stateText = componentStateText(component.state);
    const QSignalBlocker blocker(toggle);

    // checked always reflects the last confirmed status, never the requested
    // target. In particular it does not flip optimistically while pending.
    toggle->setChecked(component.confirmedState == ComponentState::On);
    toggle->setComponentState(component.state);
    toggle->setText(QStringLiteral("%1: %2").arg(name, stateText));

    const QString sourceText = component.source.isEmpty()
        ? QStringLiteral("source unavailable")
        : component.legacy
            ? QStringLiteral("legacy source")
            : component.source;
    toggle->setToolTip(
        QStringLiteral("%1; confirmed by %2").arg(stateText, sourceText));

    statusLabel->setText(
        component.legacy
            ? QStringLiteral("%1 [legacy]").arg(stateText)
            : stateText);

    QString color = QStringLiteral("#686868");
    if (component.state == ComponentState::On)
    {
        color = QStringLiteral("#167a35");
    }
    else if (component.state == ComponentState::Off)
    {
        color = QStringLiteral("#7a2525");
    }
    else if (isPendingComponentState(component.state))
    {
        color = QStringLiteral("#9a6700");
    }

    statusLabel->setStyleSheet(
        QStringLiteral("color:%1;font-weight:600;").arg(color));
}

void MainWindow::updateSystemSwitchAvailability()
{
    const bool configurationAvailable =
        controllerConnected_
        && canChangeSystemConfiguration_
        && !controllerBusy_;

    const auto stable =
        [](const ComponentViewState &component)
        {
            return isConfirmedComponentState(component.state)
                && isConfirmedComponentState(component.confirmedState);
        };

    const bool powerPending =
        isPendingComponentState(systemConfiguration_.power.state);
    const bool servoPending =
        isPendingComponentState(systemConfiguration_.servo.state);
    const bool streamPending =
        isPendingComponentState(systemConfiguration_.stream.state);

    prepareButton_->setEnabled(
        configurationAvailable
        && stable(systemConfiguration_.power)
        && stable(systemConfiguration_.servo)
        && stable(systemConfiguration_.stream)
        && !powerPending
        && !servoPending
        && !streamPending);

    powerSwitch_->setEnabled(
        configurationAvailable
        && stable(systemConfiguration_.power)
        && !servoPending
        && !streamPending);

    const bool powerConfirmedOn =
        systemConfiguration_.power.confirmedState == ComponentState::On;

    servoSwitch_->setEnabled(
        configurationAvailable
        && stable(systemConfiguration_.servo)
        && !powerPending
        && (powerConfirmedOn
            || systemConfiguration_.servo.confirmedState
                == ComponentState::On));

    streamSwitch_->setEnabled(
        configurationAvailable
        && stable(systemConfiguration_.stream)
        && !powerPending
        && (powerConfirmedOn
            || systemConfiguration_.stream.confirmedState
                == ComponentState::On));
}

void MainWindow::appendLog(
    const QString &message)
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(
            this,
            [this, message]() { appendLog(message); },
            Qt::QueuedConnection);
        return;
    }

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
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(
            this,
            [this, response]() { updateRobotStatus(response); },
            Qt::QueuedConnection);
        return;
    }

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

    setTextValue(
        robotReportedStateValueLabel_,
        {
            QStringLiteral("state"),
            QStringLiteral("robot_state"),
            QStringLiteral("control_state")
        });

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
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(
            this,
            [this, response]() { updateJointDisplay(response); },
            Qt::QueuedConnection);
        return;
    }

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

        QVector<JointAngleEdit *> &labels = it.value();

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
            if (!positions.at(index).isDouble()
                || !qIsFinite(positions.at(index).toDouble()))
            {
                continue;
            }

            const double degrees =
                radiansToDegrees(
                    positions.at(index).toDouble());

            jointConfirmedDegrees_[groupName][index] = degrees;
            labels[index]->updateConfirmedDegrees(degrees);

            sliders[index]->setProperty(
                "confirmedDegrees",
                degrees);

            if (sliders[index]->isSliderDown())
            {
                continue;
            }

            const QSignalBlocker blocker(sliders[index]);

            const int center =
                static_cast<int>(degrees * 100.0);
            const JointLimits limits =
                manualJointLimits(groupName, index);
            sliders[index]->setRange(
                static_cast<int>(limits.minimumDegrees * 100.0),
                static_cast<int>(limits.maximumDegrees * 100.0));
            sliders[index]->setValue(center);
        }
    }
}

void MainWindow::submitJointStep(const QString &groupName, int jointIndex, double stepDegrees)
{
    const auto positions = jointConfirmedDegrees_.constFind(groupName);
    if (positions == jointConfirmedDegrees_.cend()
        || jointIndex < 0 || jointIndex >= positions->size()
        || !qIsFinite(positions->at(jointIndex)))
    {
        submitJointTarget(groupName, jointIndex, 0.0);
        return;
    }
    const JointLimits limits = manualJointLimits(groupName, jointIndex);
    submitJointTarget(groupName, jointIndex,
        qBound(limits.minimumDegrees, positions->at(jointIndex) + stepDegrees,
               limits.maximumDegrees));
}

void MainWindow::submitJointTarget(
    const QString &groupName, int jointIndex, double targetDegrees)
{
    const JointLimits limits = manualJointLimits(groupName, jointIndex);
    if (!qIsFinite(targetDegrees)
        || targetDegrees < limits.minimumDegrees
        || targetDegrees > limits.maximumDegrees)
    {
        showJointMotionError(QStringLiteral("Góc %1 khớp %2 phải nằm trong [%3, %4]°.")
            .arg(groupName).arg(jointIndex + 1)
            .arg(limits.minimumDegrees).arg(limits.maximumDegrees));
        return;
    }
    const auto positions = jointConfirmedDegrees_.constFind(groupName);
    if (positions == jointConfirmedDegrees_.cend()
        || jointIndex < 0 || jointIndex >= positions->size()
        || !qIsFinite(positions->at(jointIndex)))
    {
        showJointMotionError(QStringLiteral("Chưa có góc khớp được xác nhận từ robot. Vui lòng chờ cập nhật."));
        controller_->refreshJoints();
        return;
    }
    controller_->moveJointTo(groupName, jointIndex,
        degreesToRadians(targetDegrees), minimumTimeSpinBox_->value());
}

void MainWindow::showJointMotionError(const QString &message)
{
    // Non-blocking modal dialog keeps TCP reads and status timers running.
    auto *dialog = new QMessageBox(QMessageBox::Warning,
        QStringLiteral("Không thể thay đổi góc khớp"), message, QMessageBox::Ok, this);
    dialog->setObjectName(QStringLiteral("jointMotionErrorDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void MainWindow::closeEvent(
    QCloseEvent *event)
{
    logWindow_->close();
    controller_->stopDrive();
    controller_->disconnectFromRobot();

    QMainWindow::closeEvent(event);
}
