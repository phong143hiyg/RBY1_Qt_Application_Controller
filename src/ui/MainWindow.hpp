#pragma once

#include "model/SystemStatus.hpp"

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QString>
#include <QVector>

class QCloseEvent;
class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTabWidget;
class QTextEdit;
class QWidget;
class ToggleSwitch;
class JointAngleEdit;

class RobotController;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildInterface();
    QWidget *buildSystemAndBaseTab();
    QWidget *buildUpperBodyTab();

    QGroupBox *buildJointGroup(
        const QString &title,
        const QString &groupName,
        int jointCount);

    void connectSignals();

    void applyControllerState(
        const QString &stateName,
        bool connected,
        bool canDrive,
        bool canControlJoints,
        bool canChangeSystemConfiguration,
        bool busy);

    void applySystemConfiguration(
        const SystemConfigurationView &configuration);

    void applyComponentView(
        ToggleSwitch *toggle,
        QLabel *statusLabel,
        const QString &name,
        const ComponentViewState &component);

    void updateSystemSwitchAvailability();

    void appendLog(
        const QString &message);

    void updateJointDisplay(
        const QJsonObject &response);

    void updateRobotStatus(
        const QJsonObject &response);

    void submitJointTarget(const QString &groupName, int jointIndex, double targetDegrees);
    void submitJointStep(const QString &groupName, int jointIndex, double stepDegrees);
    void showJointMotionError(const QString &message);

    RobotController *controller_{nullptr};

    QPushButton *connectButton_{nullptr};
    QComboBox *robotModelComboBox_{nullptr};
    QLineEdit *robotAddressEdit_{nullptr};
    QPushButton *pingButton_{nullptr};
    QPushButton *logButton_{nullptr};

    QPushButton *prepareButton_{nullptr};
    ToggleSwitch *powerSwitch_{nullptr};
    ToggleSwitch *servoSwitch_{nullptr};
    ToggleSwitch *streamSwitch_{nullptr};

    QPushButton *forwardButton_{nullptr};
    QPushButton *backwardButton_{nullptr};
    QPushButton *leftButton_{nullptr};
    QPushButton *rightButton_{nullptr};
    QPushButton *rotateLeftButton_{nullptr};
    QPushButton *rotateRightButton_{nullptr};
    QPushButton *stopButton_{nullptr};

    QPushButton *initialButton_{nullptr};
    QPushButton *armsReadyButton_{nullptr};
    QPushButton *setReadyButton_{nullptr};
    QPushButton *goReadyButton_{nullptr};
    QPushButton *clearReadyButton_{nullptr};

    QDoubleSpinBox *minimumTimeSpinBox_{nullptr};

    QGroupBox *systemGroup_{nullptr};
    QGroupBox *driveGroup_{nullptr};
    QGroupBox *robotStatusGroup_{nullptr};
    QWidget *upperBodyContent_{nullptr};

    QLabel *robotConnectionValueLabel_{nullptr};
    QLabel *robotControllerStateValueLabel_{nullptr};
    QLabel *robotReportedStateValueLabel_{nullptr};
    QLabel *robotReadyValueLabel_{nullptr};
    QLabel *robotPowerValueLabel_{nullptr};
    QLabel *robotServoValueLabel_{nullptr};
    QLabel *robotStreamValueLabel_{nullptr};
    QLabel *robotLastUpdateValueLabel_{nullptr};
    QLabel *robotMessageValueLabel_{nullptr};

    QHash<QString, QVector<JointAngleEdit *>> jointValueLabels_;
    QHash<QString, QVector<double>> jointConfirmedDegrees_;
    QHash<QString, QVector<QSlider *>> jointSliders_;

    QTabWidget *tabWidget_{nullptr};
    QDialog *logWindow_{nullptr};
    QTextEdit *logTextEdit_{nullptr};

    bool controllerConnected_{false};
    bool canChangeSystemConfiguration_{false};
    bool controllerBusy_{false};
    SystemConfigurationView systemConfiguration_;
};
