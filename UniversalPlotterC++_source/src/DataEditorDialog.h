#pragma once
#include <QDialog>
#include "Dataset.h"

class QComboBox;
class QListWidget;
class QLabel;
class QSpinBox;
class QLineEdit;
class MainWindow;

// "Data editor" window: pick a dataset, its X column and one or more Y columns, then create new data from them
// (n-th derivative, n-th integral, absolute value, sign flipped). The results are added to the plotter's
// data selector as a dataset called "<name> (edited)" whose columns end in (derivative), (2. derivative),
// (integral), (2. integral), (abs), (sign flipped) or (<math expression>). A plotted function is edited as math:
// the result is a new formula, e.g. sign flip of x^2 + 3*x gives -x^2 - 3*x, "*2" on sin(x) gives sin(x)*2 and
// the derivative of sin(x) is cos(x) and its integral is -cos(x) + 1 (0 at the start of the domain).
// (A result without a known formula, e.g. the integral of x*sin(x), is made as sampled points.) The edited columns are listed in this window and can be deleted here.
class DataEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit DataEditorDialog(MainWindow *app);
    void refresh();   // re-reads the datasets of the main window (called when the window is shown / activated)

protected:
    void showEvent(QShowEvent *e) override;
    void changeEvent(QEvent *e) override;

private slots:
    void onDatasetChanged();
    void onXChanged();
    void deleteSelected();
    void deleteAll();

private:
    enum class Op { Derivative, Integral, Abs, Negate, Expr };
    void apply(Op op);
    void fillYList();
    void fillEditedList();

    MainWindow *m_app;
    QComboBox *m_dsCombo = nullptr;
    QComboBox *m_xCombo = nullptr;
    QListWidget *m_yList = nullptr;
    QListWidget *m_editedList = nullptr;
    QLineEdit *m_exprEdit = nullptr;     // math expression, e.g. "*2" or "y^2 + 1"
    QSpinBox *m_orderSpin = nullptr;     // 1 = first derivative / integral, 2 = second, ...
    QLabel *m_status = nullptr;
    bool m_refreshing = false;
};
