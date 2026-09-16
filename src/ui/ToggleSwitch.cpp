#include "ToggleSwitch.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QEvent>

ToggleSwitch::ToggleSwitch(QWidget* parent) : QCheckBox(parent) {
    setCursor(Qt::PointingHandCursor);
}

ToggleSwitch::ToggleSwitch(const QString& text, QWidget* parent) : QCheckBox(text, parent) {
    setCursor(Qt::PointingHandCursor);
}

void ToggleSwitch::setComponentState(ComponentState state) {
    if (componentState_ == state) {
        return;
    }

    componentState_ = state;
    update();
}

ComponentState ToggleSwitch::componentState() const {
    return componentState_;
}

void ToggleSwitch::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Track dimensions
    int trackWidth = 40;
    int trackHeight = 20;
    int margin = 3;
    int thumbRadius = (trackHeight - 2 * margin) / 2;
    int thumbX = isChecked() ? trackWidth - margin - 2 * thumbRadius : margin;
    int thumbY = margin;

    // Draw track
    QRect trackRect(0, (height() - trackHeight) / 2, trackWidth, trackHeight);
    QPainterPath trackPath;
    trackPath.addRoundedRect(trackRect, trackHeight / 2.0, trackHeight / 2.0);
    
    p.setPen(Qt::NoPen);
    QColor trackColor;
    switch (componentState_) {
    case ComponentState::On:
        trackColor = QColor("#2EAD55");
        break;
    case ComponentState::PendingOn:
    case ComponentState::PendingOff:
        trackColor = QColor("#E5A000");
        break;
    case ComponentState::Off:
        trackColor = QColor("#737B84");
        break;
    case ComponentState::Unknown:
    default:
        trackColor = QColor("#A6A6A6");
        break;
    }

    if (!isEnabled()) {
        trackColor.setAlpha(155);
    }
    p.setBrush(trackColor);
    p.drawPath(trackPath);

    // Draw thumb
    p.setBrush(
        isEnabled()
            ? QColor(Qt::white)
            : QColor("#F0F0F0"));
    p.drawEllipse(QRectF(trackRect.x() + thumbX, trackRect.y() + thumbY, 2 * thumbRadius, 2 * thumbRadius));

    // Draw text
    QRect textRect = rect();
    textRect.setLeft(trackRect.right() + 8);
    p.setPen(
        palette().color(
            isEnabled()
                ? QPalette::Active
                : QPalette::Disabled,
            QPalette::WindowText));
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text());
}

QSize ToggleSwitch::sizeHint() const {
    QFontMetrics fm(font());
    int w = 40 + 8 + fm.horizontalAdvance(text());
    int h = qMax(20, fm.height());
    return QSize(w, h);
}

bool ToggleSwitch::hitButton(const QPoint &pos) const {
    return rect().contains(pos);
}
