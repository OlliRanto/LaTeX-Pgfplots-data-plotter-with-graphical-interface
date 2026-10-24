#include "ColorPickerWidget.h"
#include "LatexColors.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QColorDialog>
#include <QRegularExpression>
#include <QEvent>
#include <QFrame>

ColorPickerWidget::ColorPickerWidget(const QString &initialNameOrHex, QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QString initHex, initDisplay;
    if (latexColors().contains(initialNameOrHex)) {
        initHex = latexColors()[initialNameOrHex];
        initDisplay = initialNameOrHex;
    } else {
        QString h = initialNameOrHex.trimmed();
        h.remove('#');
        if (isHexColor(h)) { initHex = h.toUpper(); initDisplay = "Custom..."; }
        else { initHex = latexColors()["blue"]; initDisplay = "blue"; }
    }
    m_currentToken = (initDisplay == "Custom...") ? initHex : initDisplay;

    m_combo = new QComboBox;
    QStringList names = latexColors().keys();
    names.sort();
    names << "Custom...";
    m_combo->addItems(names);
    m_combo->setCurrentText(initDisplay);
    m_combo->setMaximumWidth(95);
    layout->addWidget(m_combo);

    m_swatch = new QLabel;
    m_swatch->setFixedSize(18, 18);
    m_swatch->setFrameShape(QFrame::Panel);
    m_swatch->setFrameShadow(QFrame::Sunken);
    m_swatch->setCursor(Qt::PointingHandCursor);
    m_swatch->setStyleSheet(QString("background-color:#%1;").arg(initHex));
    layout->addWidget(m_swatch);

    m_hexEdit = new QLineEdit(initHex);
    m_hexEdit->setMaximumWidth(60);
    layout->addWidget(m_hexEdit);

    connect(m_combo, &QComboBox::currentTextChanged, this, &ColorPickerWidget::onNamePicked);
    connect(m_hexEdit, &QLineEdit::editingFinished, this, &ColorPickerWidget::onHexEdited);
    m_swatch->installEventFilter(this);
}

void ColorPickerWidget::updateSwatch(const QString &hex) {
    if (isHexColor(hex)) m_swatch->setStyleSheet(QString("background-color:#%1;").arg(hex.toUpper()));
}

void ColorPickerWidget::onNamePicked(const QString &name) {
    if (name == "Custom...") { openPicker(); return; }
    if (latexColors().contains(name)) {
        m_hexEdit->setText(latexColors()[name]);
        m_currentToken = name;
        updateSwatch(latexColors()[name]);
    }
}

void ColorPickerWidget::onHexEdited() {
    QString h = m_hexEdit->text().trimmed();
    h.remove('#');
    if (isHexColor(h)) {
        h = h.toUpper();
        m_hexEdit->setText(h);
        m_currentToken = h;
        updateSwatch(h);
        m_combo->blockSignals(true);
        m_combo->setCurrentText("Custom...");
        m_combo->blockSignals(false);
    }
}

void ColorPickerWidget::openPicker() {
    QString cur = m_hexEdit->text().trimmed();
    cur.remove('#');
    QColor initial = isHexColor(cur) ? QColor("#" + cur) : QColor("#0000FF");
    QColor chosen = QColorDialog::getColor(initial, this, "Choose color");
    if (!chosen.isValid()) {
        // user cancelled -- keep whatever was selected before
        m_combo->blockSignals(true);
        m_combo->setCurrentText(latexColors().contains(m_currentToken) ? m_currentToken : "Custom...");
        m_combo->blockSignals(false);
        return;
    }
    QString hex = chosen.name(QColor::HexRgb).mid(1).toUpper();
    m_hexEdit->setText(hex);
    m_currentToken = hex;
    updateSwatch(hex);
    m_combo->blockSignals(true);
    m_combo->setCurrentText("Custom...");
    m_combo->blockSignals(false);
}

bool ColorPickerWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_swatch && event->type() == QEvent::MouseButtonPress) {
        openPicker();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

QString ColorPickerWidget::colorToken() const { return m_currentToken; }
void ColorPickerWidget::setColorToken(const QString &nameOrHex) {
    if (latexColors().contains(nameOrHex)) {
        m_combo->setCurrentText(nameOrHex);
    } else {
        QString h = nameOrHex; h.remove('#');
        if (isHexColor(h)) {
            m_hexEdit->setText(h.toUpper());
            onHexEdited();
        }
    }
}
