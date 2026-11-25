#pragma once
#include <QDialog>
#include <QColor>
#include <QString>
#include <QVector>

class QListWidget;
class QSpinBox;
class QLabel;
class QRadioButton;
class QLineEdit;
class QPushButton;

// One selectable column of the picker.
struct StatColumn {
    QString label;             // shown in the list ("file: column")
    QColor color;              // text colour in the list (invalid = default)
    QVector<double> values;    // one per point; NaN = empty / not a number
    QVector<double> x;         // X of every point (empty = no X known)
    QString xName;
};

// "Mean / standard deviation of a column": a column list like the Y selector of the main window, then the first and
// last point of the range to use, with a live preview of the result. Used by the right-click menu of the
// Enter / edit data table, which writes the result into the clicked cell.
class ColumnStatDialog : public QDialog {
public:
    enum class Kind { Mean, StdDev };

    ColumnStatDialog(Kind kind, const QString &targetCell, const QVector<StatColumn> &columns, QWidget *parent = nullptr);

    bool hasResult() const { return m_valid; }
    double result() const { return m_result; }
    QString resultText() const;                       // the number as it is written into the cell

    // Mean or standard deviation of the points from..to (1-based, inclusive). Empty / non-numeric values are skipped.
    // sample: divide by n-1 (otherwise n). Gives NaN when there are too few values. `used` receives the value count.
    static double compute(const QVector<double> &values, int from, int to, Kind kind, bool sample, int *used = nullptr);

    // Same, for the points whose X lies in [xlo, xhi] (inclusive, any order of the x values). `total` receives the number
    // of points inside the X range (including empty values).
    static double computeByX(const QVector<double> &values, const QVector<double> &x, double xlo, double xhi, Kind kind,
                             bool sample, int *used = nullptr, int *total = nullptr);

    // widgets (public so that the dialog can be driven from a test)
    QListWidget *list = nullptr;
    QSpinBox *fromSpin = nullptr, *toSpin = nullptr;
    QRadioButton *sampleRadio = nullptr, *populationRadio = nullptr;
    QRadioButton *byPointRadio = nullptr, *byXRadio = nullptr;
    QLineEdit *xFromEdit = nullptr, *xToEdit = nullptr;
    QPushButton *okButton = nullptr;
    QLabel *resultLabel = nullptr;

private:
    void columnChanged();                             // the list selection changed: reset the range
    void modeChanged();                               // point numbers <-> X values
    void update();                                    // recompute the preview
    Kind m_kind;
    QVector<StatColumn> m_cols;
    QLabel *m_rangeInfo = nullptr, *m_xInfo = nullptr;
    double m_result = 0;
    bool m_valid = false;
};
