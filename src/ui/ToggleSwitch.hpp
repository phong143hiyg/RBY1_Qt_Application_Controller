#pragma once

#include "model/SystemStatus.hpp"

#include <QCheckBox>
#include <QWidget>

class ToggleSwitch : public QCheckBox {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    explicit ToggleSwitch(const QString& text, QWidget* parent = nullptr);

    void setComponentState(ComponentState state);
    [[nodiscard]] ComponentState componentState() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;
    bool hitButton(const QPoint &pos) const override;

private:
    ComponentState componentState_{ComponentState::Unknown};
};
