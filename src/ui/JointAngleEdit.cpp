#include "ui/JointAngleEdit.hpp"

#include <QFocusEvent>

JointAngleEdit::JointAngleEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setText(confirmedText_);
    setAlignment(Qt::AlignRight);
    setMaxLength(32);
}

void JointAngleEdit::updateConfirmedDegrees(double degrees)
{
    confirmedText_ = QString::number(degrees, 'f', 2);
    if (!hasFocus())
    {
        setText(confirmedText_);
    }
}

void JointAngleEdit::focusInEvent(QFocusEvent *event)
{
    QLineEdit::focusInEvent(event);
    selectAll();
}

void JointAngleEdit::clearConfirmedDegrees()
{
    confirmedText_ = QStringLiteral("--");
    setText(confirmedText_);
}

void JointAngleEdit::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
    // Losing focus discards the draft. It must never submit a command.
    setText(confirmedText_);
}
