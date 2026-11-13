#pragma once
#include <QObject>
#include <QEvent>
#include <QComboBox>
#include <QAbstractSpinBox>

// Installed on the QApplication instance. Scrolling the mouse wheel over a
// combo box or spin box normally changes its value, which is a common
// source of accidental changes when the widget just happens to be under
// the cursor while scrolling a panel. This filter swallows wheel events
// aimed at any QComboBox or QAbstractSpinBox (which covers QSpinBox and
// QDoubleSpinBox) so the wheel only ever scrolls the containing panel,
// never the widget's value.
class NoWheelComboFilter : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Wheel &&
            (qobject_cast<QComboBox *>(watched) || qobject_cast<QAbstractSpinBox *>(watched))) {
            event->ignore();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};
