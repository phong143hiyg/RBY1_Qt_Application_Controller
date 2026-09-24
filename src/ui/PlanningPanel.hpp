#pragma once
#include <QWidget>
#include <QHash>
#include <QVector>
#include <QJsonObject>
class PlanningClient;
class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QPlainTextEdit;
class QProgressBar;

class PlanningPanel final : public QWidget {
    Q_OBJECT
public:
    explicit PlanningPanel(QWidget *parent = nullptr);
private:
    void updateView();
    void fillScene();
    void submit(const QString &command);
    QJsonObject readPose(const QVector<QLineEdit *> &edits, const QString &frame, bool &valid);
    PlanningClient *client_;
    QLineEdit *host_, *port_, *pickFrame_, *placeFrame_;
    QComboBox *scenario_, *object_;
    QLabel *capability_, *state_, *directions_, *stage_;
    QProgressBar *progress_;
    QTableWidget *objects_;
    QPlainTextEdit *results_, *log_;
    QPushButton *connect_, *load_, *pose_, *task_, *preview_, *cancel_, *reconcile_;
    QVector<QLineEdit *> pick_, place_;
    QHash<QString, QLineEdit *> parameters_;
    QString displayedRevision_;
};
