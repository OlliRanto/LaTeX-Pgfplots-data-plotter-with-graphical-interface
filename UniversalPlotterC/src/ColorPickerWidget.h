#pragma once
#include <QWidget>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;

// A dropdown of the 19 base LaTeX color names plus "Custom...", a clickable
// swatch, and a hex entry -- mirrors LatexExportDialog._make_color_combo()
// from the Python app. colorToken() returns either a base color name
// (e.g. "blue") or a bare 6-digit hex code for a custom color.
class ColorPickerWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorPickerWidget(const QString &initialNameOrHex, QWidget *parent = nullptr);
    QString colorToken() const;
    void setColorToken(const QString &nameOrHex);

signals:
    // Emitted whenever the color changes (typed, picked from the list or chosen in the dialog).
    // Also emitted for changes made through setColorToken().
    void colorChanged(const QString &token);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onNamePicked(const QString &name);
    void onHexEdited();
    void openPicker();

private:
    void updateSwatch(const QString &hex);

    QComboBox *m_combo;
    QLabel *m_swatch;
    QLineEdit *m_hexEdit;
    QString m_currentToken; // name or hex, no '#'
};
