#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QString>
#include <QVector>

class QCloseEvent;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTextEdit;
class QWidget;
class ToggleSwitch;

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
        bool powerEnabled,
        bool servoEnabled,
        bool streamEnabled);

    void updateSystemSwitchAvailability();

    void appendLog(
        const QString &message);

    void updateJointDisplay(
        const QJsonObject &response);

    RobotController *controller_{nullptr};

    QLabel *connectionStatusLabel_{nullptr};
    QLabel *stateLabel_{nullptr};

    QPushButton *connectButton_{nullptr};
    QPushButton *pingButton_{nullptr};

    QPushButton *prepareButton_{nullptr};
    ToggleSwitch *powerSwitch_{nullptr};
    ToggleSwitch *servoSwitch_{nullptr};
    ToggleSwitch *streamSwitch_{nullptr};
    QPushButton *cancelButton_{nullptr};

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

    QDoubleSpinBox *jointStepSpinBox_{nullptr};
    QDoubleSpinBox *minimumTimeSpinBox_{nullptr};

    QGroupBox *systemGroup_{nullptr};
    QGroupBox *driveGroup_{nullptr};
    QWidget *upperBodyContent_{nullptr};

    QHash<QString, QVector<QLabel *>> jointValueLabels_;

    QTabWidget *tabWidget_{nullptr};
    QTextEdit *logTextEdit_{nullptr};

    bool controllerConnected_{false};
    bool canChangeSystemConfiguration_{false};
    bool controllerBusy_{false};
    bool powerEnabled_{false};
};
