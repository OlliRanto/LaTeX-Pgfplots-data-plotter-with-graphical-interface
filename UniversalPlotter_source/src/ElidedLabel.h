#pragma once
#include <QLabel>
#include <QFontMetrics>

// A QLabel that never forces its window wider: long text is cut with "..." to the width the layout gives it
// (the full text is the tooltip). text() returns what is displayed; fullText() the whole string.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(const QString &text = QString(), QWidget *parent = nullptr) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        setText(text);
    }
    void setText(const QString &t) {
        m_full = t;
        setToolTip(t);
        refresh();
    }
    QString fullText() const { return m_full; }
    QSize minimumSizeHint() const override { return QSize(40, QLabel::minimumSizeHint().height()); }
    QSize sizeHint() const override {
        QSize s = QLabel::sizeHint();
        s.setWidth(fontMetrics().horizontalAdvance(m_full) + 2 * margin() + 2);
        return s;
    }
protected:
    void resizeEvent(QResizeEvent *e) override { QLabel::resizeEvent(e); refresh(); }
private:
    void refresh() {
        QLabel::setText(fontMetrics().elidedText(m_full, Qt::ElideMiddle, std::max(10, width() - 2 * margin() - 2)));
    }
    QString m_full;
};
