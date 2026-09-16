#pragma once

#include <QLineEdit>

// Text under edit is a draft, never a confirmed robot position.
class JointAngleEdit final : public QLineEdit
{
public:
    explicit JointAngleEdit(QWidget *parent = nullptr);
    void updateConfirmedDegrees(double degrees);
    void clearConfirmedDegrees();

protected:
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    QString confirmedText_{QStringLiteral("--")};
};
