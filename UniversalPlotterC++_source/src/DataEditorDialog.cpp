#include "DataEditorDialog.h"
#include "MainWindow.h"
#include "ExprEval.h"
#include "ExprAst.h"

#include <QComboBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QRegularExpression>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QEvent>
#include <QPair>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

int rowCount(const Dataset &ds) { return ds.columns.isEmpty() ? 0 : ds.columns.first().values.size(); }

// Text in the data selector for a dataset (functions have no file name).
QString datasetTitle(const Dataset &ds) { return ds.isFunction ? QString("f(x) = ") + ds.funcExpr : ds.basename; }

// Result of an operation on y(x) (one value per input row; NaN where it is not defined).
// Derivatives and the integral are computed along increasing x, whatever the row order is.
// Returns false (and an error text) when there are too few valid points.
bool compute(int op, const QVector<double> &x, const QVector<double> &y, QVector<double> &out, QString &error) {
    const int n = y.size();
    out = QVector<double>(n, kNaN);
    if (op == 3 || op == 4) {                                   // abs / sign flip need no x
        for (int i = 0; i < n; ++i)
            if (std::isfinite(y[i])) out[i] = (op == 3) ? std::fabs(y[i]) : 0.0 - y[i];   // (0 - y: no "-0")
        return true;
    }
    QVector<int> idx;
    for (int i = 0; i < n; ++i)
        if (std::isfinite(x[i]) && std::isfinite(y[i])) idx << i;
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return x[a] < x[b]; });
    const int m = idx.size();
    auto X = [&](int k) { return x[idx[k]]; };
    auto Y = [&](int k) { return y[idx[k]]; };

    if (op == 0) {                                              // derivative dy/dx
        if (m < 2) { error = "needs at least 2 points with valid X and Y values"; return false; }
        for (int k = 0; k < m; ++k) {
            const int a = std::max(k - 1, 0), b = std::min(k + 1, m - 1);   // central inside, one-sided at the ends
            const double dx = X(b) - X(a);
            if (dx != 0) out[idx[k]] = (Y(b) - Y(a)) / dx;
        }
    } else if (op == 1) {                                       // second derivative d2y/dx2 (uneven spacing allowed)
        if (m < 3) { error = "needs at least 3 points with valid X and Y values"; return false; }
        for (int k = 1; k < m - 1; ++k) {
            const double h1 = X(k) - X(k - 1), h2 = X(k + 1) - X(k);
            if (h1 > 0 && h2 > 0)
                out[idx[k]] = 2.0 * ((Y(k + 1) - Y(k)) / h2 - (Y(k) - Y(k - 1)) / h1) / (h1 + h2);
        }
        out[idx[0]] = out[idx[1]];                              // the ends have no second difference: take the neighbour
        out[idx[m - 1]] = out[idx[m - 2]];
    } else {                                                    // cumulative integral (trapezoid rule), 0 at the first point
        if (m < 2) { error = "needs at least 2 points with valid X and Y values"; return false; }
        double acc = 0;
        out[idx[0]] = 0;
        for (int k = 1; k < m; ++k) {
            acc += 0.5 * (Y(k) + Y(k - 1)) * (X(k) - X(k - 1));
            out[idx[k]] = acc;
        }
    }
    return true;
}

// n-th derivative (op 0) or n-th integral (op 2): the single step applied n times; abs (3) and sign flip (4) once. A derivative of order 2k uses the
// second derivative k times (more accurate than two first differences), an odd order ends with one first derivative.
bool computeOrder(int op, int order, const QVector<double> &x, const QVector<double> &y, QVector<double> &out, QString &error) {
    QVector<double> cur = y;
    auto step = [&](int kind) {
        QVector<double> next;
        if (!compute(kind, x, cur, next, error)) return false;
        cur = next;
        return true;
    };
    if (op == 0) {
        for (int k = 0; k < order / 2; ++k) if (!step(1)) return false;
        if (order % 2) if (!step(0)) return false;
    } else if (op == 2) {
        for (int k = 0; k < order; ++k) if (!step(2)) return false;
    } else {
        if (!step(op)) return false;                    // abs (3) / sign flip (4): one step, the order is ignored
    }
    out = cur;
    return true;
}

// The text typed in the math box as a full expression of y (and x). A text that starts with an operator is applied to y:
// "*2" -> "y*2", "+5" -> "y+5", "/3" -> "y/3", "^2" -> "y^2", "-1" -> "y-1".
// Anything else must contain y itself ("y^2 + 1", "sqrt(y)", "2*y + x").
bool fullExpression(const QString &textIn, QString &full, QString &error) {
    const QString t = textIn.trimmed();
    if (t.isEmpty()) { error = "Type a math expression first, for example  *2  or  y^2 + 1."; return false; }
    if (QString("*/+-^").contains(t[0])) full = "y" + t;
    else full = t;
    if (!full.contains(QRegularExpression("\\by\\b"))) {
        error = "The expression must use y for the value, or start with an operator.\nExamples:  *2   +5   /3   ^2   y^2 + 1   sqrt(y)   2*y + x";
        return false;
    }
    try {
        ExprEval(full).evaluate({{"x", 1.0}, {"y", 1.0}});   // catches syntax errors and unknown names
        expr::parse(full.toStdString());
    } catch (std::exception &e) {
        error = QString("Cannot read the expression \"%1\":\n%2").arg(t, e.what());
        return false;
    }
    return true;
}

} // namespace

DataEditorDialog::DataEditorDialog(MainWindow *app) : QDialog(app), m_app(app) {
    setWindowTitle("Data editor");
    resize(560, 640);

    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(
        "Choose a dataset, its X column and the Y column(s), then press an operation or type a math expression. "
        "The result appears in the plot window's data selector as a new column with the operation added to its name. "
        "A plotted function f(x) is edited as math: the result is a new formula (for example -sin(x) or 2*sin(x)), "
        "not a set of points.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#777;");
    layout->addWidget(hint);

    // ---- data selector ----
    auto *dataBox = new QGroupBox("Data");
    auto *dl = new QVBoxLayout(dataBox);
    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel("Dataset:"));
    m_dsCombo = new QComboBox;
    m_dsCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_dsCombo->setMinimumContentsLength(14);
    row1->addWidget(m_dsCombo, 1);
    row1->addWidget(new QLabel("X column:"));
    m_xCombo = new QComboBox;
    m_xCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_xCombo->setMinimumContentsLength(10);
    row1->addWidget(m_xCombo, 1);
    dl->addLayout(row1);
    dl->addWidget(new QLabel("Y column(s) (select one or more):"));
    m_yList = new QListWidget;
    m_yList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_yList->setMinimumHeight(120);
    dl->addWidget(m_yList);
    layout->addWidget(dataBox, 1);

    // ---- operations ----
    auto *opBox = new QGroupBox("Operation on the selected Y column(s)");
    auto *ol = new QHBoxLayout(opBox);
    ol->addWidget(new QLabel("Order:"));
    m_orderSpin = new QSpinBox;
    m_orderSpin->setRange(1, 10);
    m_orderSpin->setValue(1);
    m_orderSpin->setMinimumWidth(60);
    m_orderSpin->setToolTip("For Derivative and Integral: 1 = first, 2 = second, 3 = third, ...\n"
                            "(Abs and Flip sign ignore it.)");
    ol->addWidget(m_orderSpin);
    auto addOp = [&](const QString &text, const QString &tip, Op op) {
        auto *b = new QPushButton(text);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, [this, op]() { apply(op); });
        ol->addWidget(b);
    };
    addOp("Derivative", "n-th derivative d\xE2\x81\xBF" "y/dx\xE2\x81\xBF (central differences, one-sided at the ends)\n"
                        "name (derivative), (2. derivative), (3. derivative), ...", Op::Derivative);
    addOp("Integral", "n-th cumulative integral (trapezoid rule, 0 at the first point)\n"
                      "name (integral), (2. integral), (3. integral), ...", Op::Integral);
    addOp("Abs", "|y|  ->  name (abs)", Op::Abs);
    addOp("Flip sign", "-y: every + becomes - and every - becomes +  ->  name (sign flipped)\n"
                    "(for a plotted function the formula changes sign: x^2 + 3*x  ->  -x^2 - 3*x)", Op::Negate);
    layout->addWidget(opBox);

    // ---- math expression ----
    auto *exprBox = new QGroupBox("Math expression on the selected Y column(s)");
    auto *xl = new QHBoxLayout(exprBox);
    xl->addWidget(new QLabel("y ="));
    m_exprEdit = new QLineEdit;
    m_exprEdit->setPlaceholderText("e.g.  *2   +5   /3   ^2   or   y^2 + 1,  sqrt(y),  2*y + x");
    m_exprEdit->setToolTip("The new value of every point, calculated from the old one.\n"
                           "y is the selected Y column, x the X column.\n"
                           "Starting with an operator applies it to y:  *2 doubles it,  +5 adds 5,  /3 divides by 3,\n"
                           "^2 squares it,  -1 subtracts 1.  Or write a full expression:  y^2 + 1,  sqrt(y),  sin(x)*y.\n"
                           "Functions: sin cos tan asin acos atan sinh cosh tanh exp log log10 log2 sqrt abs; constants pi, e.\n"
                           "For a plotted function the result is a new formula, e.g.  *2  on sin(x)  gives  sin(x)*2.");
    xl->addWidget(m_exprEdit, 1);
    auto *exprBtn = new QPushButton("Apply");
    exprBtn->setToolTip("Apply the expression to the selected Y column(s)  ->  name (expression)");
    xl->addWidget(exprBtn);
    layout->addWidget(exprBox);
    connect(exprBtn, &QPushButton::clicked, this, [this]() { apply(Op::Expr); });
    connect(m_exprEdit, &QLineEdit::returnPressed, this, [this]() { apply(Op::Expr); });

    // ---- edited data ----
    auto *editedBox = new QGroupBox("Edited data (also shown in the plot window)");
    auto *el = new QVBoxLayout(editedBox);
    m_editedList = new QListWidget;
    m_editedList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_editedList->setMinimumHeight(120);
    el->addWidget(m_editedList);
    auto *eb = new QHBoxLayout;
    auto *delBtn = new QPushButton("Delete selected");
    auto *delAllBtn = new QPushButton("Delete all edited data");
    eb->addWidget(delBtn);
    eb->addWidget(delAllBtn);
    eb->addStretch();
    el->addLayout(eb);
    layout->addWidget(editedBox, 1);

    m_status = new QLabel;
    m_status->setStyleSheet("color:#555;");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    connect(m_dsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DataEditorDialog::onDatasetChanged);
    connect(m_xCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DataEditorDialog::onXChanged);
    connect(delBtn, &QPushButton::clicked, this, &DataEditorDialog::deleteSelected);
    connect(delAllBtn, &QPushButton::clicked, this, &DataEditorDialog::deleteAll);

    refresh();
}

void DataEditorDialog::showEvent(QShowEvent *e) {
    QDialog::showEvent(e);
    refresh();
}

void DataEditorDialog::changeEvent(QEvent *e) {
    QDialog::changeEvent(e);
    if (e->type() == QEvent::ActivationChange && isActiveWindow()) refresh();   // data may have been loaded meanwhile
}

// Re-fills the selectors from the main window's datasets, keeping the current choices where they still exist.
void DataEditorDialog::refresh() {
    if (m_refreshing) return;
    m_refreshing = true;
    const QVector<Dataset> &all = m_app->datasets();
    const QString keepDs = m_dsCombo->currentText();

    m_dsCombo->clear();
    for (int i = 0; i < all.size(); ++i) m_dsCombo->addItem(datasetTitle(all[i]), i);
    int pos = m_dsCombo->findText(keepDs);
    if (pos < 0) pos = 0;
    if (m_dsCombo->count() > 0) m_dsCombo->setCurrentIndex(pos);
    m_refreshing = false;

    onDatasetChanged();      // fills the X combo and the Y list
    fillEditedList();
}

void DataEditorDialog::onDatasetChanged() {
    if (m_refreshing) return;
    m_refreshing = true;
    const QString keepX = m_xCombo->currentText();
    m_xCombo->clear();
    const int dsIdx = m_dsCombo->currentData().toInt();
    const QVector<Dataset> &all = m_app->datasets();
    if (m_dsCombo->count() > 0 && dsIdx >= 0 && dsIdx < all.size()) {
        const Dataset &ds = all[dsIdx];
        m_xCombo->addItem(indexLabel());
        for (const auto &c : ds.columns) if (c.isNumeric) m_xCombo->addItem(c.name);
        int p = m_xCombo->findText(keepX);
        if (p < 0) p = m_xCombo->findText(ds.xColumn);
        m_xCombo->setCurrentIndex(p < 0 ? 0 : p);
    }
    m_refreshing = false;
    fillYList();
}

void DataEditorDialog::onXChanged() {
    if (m_refreshing) return;
    fillYList();
}

void DataEditorDialog::fillYList() {
    QStringList keep;
    for (auto *it : m_yList->selectedItems()) keep << it->text();
    m_yList->clear();
    const int dsIdx = m_dsCombo->currentData().toInt();
    const QVector<Dataset> &all = m_app->datasets();
    if (m_dsCombo->count() == 0 || dsIdx < 0 || dsIdx >= all.size()) return;
    const Dataset &ds = all[dsIdx];
    for (const auto &c : ds.columns) {
        if (!c.isNumeric || c.name == m_xCombo->currentText()) continue;
        auto *item = new QListWidgetItem(c.name);
        m_yList->addItem(item);
        if (keep.contains(c.name)) item->setSelected(true);
    }
}

void DataEditorDialog::fillEditedList() {
    m_editedList->clear();
    const QVector<Dataset> &all = m_app->datasets();
    for (int i = 0; i < all.size(); ++i) {
        const Dataset &ds = all[i];
        if (!ds.isEdited) continue;
        for (const auto &c : ds.columns) {
            if (c.name == ds.xColumn) continue;         // the X column is only there to go with the results
            auto *item = new QListWidgetItem(ds.isFunction ? QString("f(x) = ") + ds.funcExpr
                                                           : ds.basename + ":  " + c.name + "    [X: " + ds.xColumn + "]");
            item->setData(Qt::UserRole, i);
            item->setData(Qt::UserRole + 1, c.name);
            m_editedList->addItem(item);
        }
    }
    m_status->setText(m_editedList->count() == 0 ? "No edited data yet."
                                                  : QString("%1 edited column(s).").arg(m_editedList->count()));
}

void DataEditorDialog::apply(Op opEnum) {
    // internal kinds: 0 derivative, 2 integral, 3 abs, 4 sign flip, 5 math expression
    // (1 = second derivative is only used inside computeOrder)
    static const int kind[5] = {0, 2, 3, 4, 5};
    const int op = kind[int(opEnum)];
    QString userExpr, fullExpr;
    if (op == 5) {
        QString err;
        if (!fullExpression(m_exprEdit->text(), fullExpr, err)) {
            QMessageBox::information(this, "Math expression", err);
            return;
        }
        userExpr = m_exprEdit->text().trimmed();
    }
    const int order = (op == 0 || op == 2) ? m_orderSpin->value() : 1;
    const QVector<Dataset> &all = m_app->datasets();
    const int dsIdx = m_dsCombo->currentData().toInt();
    if (m_dsCombo->count() == 0 || dsIdx < 0 || dsIdx >= all.size()) {
        QMessageBox::information(this, "No data", "Load a file, enter data or plot a function first.");
        return;
    }
    QStringList yNames;
    for (auto *it : m_yList->selectedItems()) yNames << it->text();
    if (yNames.isEmpty()) {
        QMessageBox::information(this, "No Y column", "Select one or more Y columns first.");
        return;
    }

    const Dataset src = all[dsIdx];                      // (copy: the main window replaces its datasets below)
    const QString xName = m_xCombo->currentText();
    const int n = rowCount(src);
    QVector<double> x(n);
    if (xName == indexLabel()) {
        for (int i = 0; i < n; ++i) x[i] = i;
    } else {
        const int xi = src.columnIndex(xName);
        if (xi < 0 || !src.columns[xi].isNumeric) {
            QMessageBox::warning(this, "X column", "The X column must be numeric.");
            return;
        }
        x = src.columns[xi].values;
    }

    // " (derivative)", " (2. derivative)", " (integral)", " (3. integral)", " (abs)", " (sign flipped)"
    auto suffixFor = [&]() {
        auto ord = [&](const char *what) { return order == 1 ? QString(" (%1)").arg(what) : QString(" (%1. %2)").arg(order).arg(what); };
        switch (op) {
        case 0: return ord("derivative");
        case 2: return ord("integral");
        case 3: return QString(" (abs)");
        case 5: return QString(" (%1)").arg(userExpr);
        default: return QString(" (sign flipped)");
        }
    };
    const QString suffix = suffixFor();

    // A plotted function is edited as MATH: the result is a new formula (-sin(x), sin(x)*2, cos(x), ...) that is
    // plotted, fitted and exported to LaTeX like any typed function. Only an integral / derivative that cannot
    // be found as a formula (e.g. x*sin(x)) falls through to sampled points below.
    QString noFormulaNote;
    if (src.isFunction) {
        const int yi = src.columnIndex("f(x)");
        const int xi = src.columnIndex(src.xColumn);
        if (yi < 0 || xi < 0 || !yNames.contains("f(x)")) {
            QMessageBox::information(this, "Select f(x)", "Select the f(x) column of the function.");
            return;
        }
        expr::NodeP result;
        try {
            const expr::NodeP f = expr::parse(src.funcExpr.toStdString());
            switch (op) {
            case 0:
                result = f;
                for (int k = 0; k < order && result; ++k) result = expr::derivative(result, "x");
                if (result) result = expr::simplify(result);
                else noFormulaNote = "(no formula for this derivative: sampled points were made)";
                break;
            case 2: {                                        // n-th integral: antiderivative, 0 at the start of the domain
                result = f;
                for (int k = 0; k < order && result; ++k) {
                    expr::NodeP F = expr::integral(result, "x");
                    if (!F) { result = nullptr; break; }
                    double c = 0;
                    try { c = ExprEval(QString::fromStdString(expr::print(F))).evaluate({{"x", src.funcMin}}); } catch (...) {}
                    if (!std::isfinite(c) || std::fabs(c) < 1e-12) c = 0;
                    result = expr::simplify(expr::addConstant(F, -c));
                }
                if (!result) noFormulaNote = "(no formula found for this integral: sampled points were made)";
                break;
            }
            case 3: result = expr::absolute(f); break;
            case 4: result = expr::negate(f); break;
            case 5: result = expr::simplify(expr::substitute(expr::parse(fullExpr.toStdString()), "y", f)); break;
            default: break;
            }
        } catch (std::exception &e) {
            QMessageBox::warning(this, "Could not edit", QString("f(x): ") + e.what());
            return;
        }
        if (result) {
            const QString newExpr = QString::fromStdString(expr::print(result));
            try { ExprEval(newExpr).evaluate({{"x", 0.5 * (src.funcMin + src.funcMax)}}); }
            catch (std::exception &e) {
                QMessageBox::warning(this, "Could not edit", QString("The new formula %1 cannot be evaluated:\n%2").arg(newExpr, e.what()));
                return;
            }
            Dataset out = src;                                // keeps the domain / samples of the function
            out.isEdited = true;
            out.isFunction = true;
            out.path = editedDataPath();
            out.funcExpr = newExpr;
            out.basename = newExpr;
            ColumnData xc = src.columns[xi], yc;
            yc.name = "f(x)";
            const ExprEval ev(newExpr);
            for (double xv : xc.values) {
                double yv = kNaN;
                try { yv = ev.evaluate({{"x", xv}}); } catch (...) {}
                yc.values << (std::isfinite(yv) ? yv : kNaN);
            }
            out.columns.clear();
            out.columns.push_back(xc);
            out.columns.push_back(yc);
            out.xColumn = xc.name;
            out.info.rows = xc.values.size();
            out.info.cols = 2;
            m_app->addEditedDataset(out);
            refresh();
            m_status->setText("Added: f(x) = " + newExpr);
            return;
        }
    }

    // The results of one source go into one dataset "<name> (edited)" (one per X column).
    // Editing edited data again adds the new column to that same dataset.
    Dataset out;
    bool found = false;
    if (src.isEdited && src.xColumn == xName) {
        out = src;
        found = true;
    } else {
        const QString base = src.isEdited ? src.basename : src.basename + " (edited)";
        for (const auto &d : all)
            if (d.isEdited && !d.isFunction && d.basename == base && d.xColumn == xName) { out = d; found = true; break; }
        if (!found) {
            out.isEdited = true;
            out.path = editedDataPath();
            out.basename = base;
            out.xColumn = xName;
            out.info.sep = ",";
            out.info.decimal = ".";
            out.info.hasHeader = true;
            out.info.encoding = "utf-8";
            if (xName != indexLabel()) out.columns.push_back(src.columns[src.columnIndex(xName)]);
        }
    }

    QStringList done, failed;
    for (const QString &yName : yNames) {
        const int yi = src.columnIndex(yName);
        if (yi < 0) continue;
        QVector<double> res;
        QString err;
        if (op == 5) {                                       // math expression: evaluated for every row
            const QVector<double> &yv = src.columns[yi].values;
            const ExprEval ev(fullExpr);
            res = QVector<double>(n, kNaN);
            for (int i = 0; i < n && i < yv.size(); ++i) {
                if (!std::isfinite(yv[i])) continue;
                try {
                    const double r = ev.evaluate({{"x", x[i]}, {"y", yv[i]}});
                    if (std::isfinite(r)) res[i] = r;
                } catch (...) {}
            }
        } else if (!computeOrder(op, order, x, src.columns[yi].values, res, err)) { failed << yName + ": " + err; continue; }
        ColumnData col;
        col.name = yName + suffix;
        if (op == 4 && yName.endsWith(suffix)) col.name = yName.left(yName.size() - suffix.size());   // flipping twice: back to the original name
        col.values = res;
        col.isNumeric = true;
        const int existing = out.columnIndex(col.name);
        if (existing >= 0) out.columns[existing] = col;      // same operation again: update instead of adding a copy
        else out.columns.push_back(col);
        done << col.name;
    }
    if (!failed.isEmpty())
        QMessageBox::warning(this, "Could not edit", failed.join("\n"));
    if (done.isEmpty()) return;

    out.info.rows = n;
    out.info.cols = out.columns.size();
    m_app->addEditedDataset(out);
    refresh();
    m_status->setText("Added: " + done.join(", ") + (noFormulaNote.isEmpty() ? QString() : "  " + noFormulaNote));
}

void DataEditorDialog::deleteSelected() {
    QVector<QPair<int, QString>> items;
    for (auto *it : m_editedList->selectedItems())
        items.push_back({it->data(Qt::UserRole).toInt(), it->data(Qt::UserRole + 1).toString()});
    if (items.isEmpty()) {
        QMessageBox::information(this, "Nothing selected", "Select the edited data to delete in the list first.");
        return;
    }
    m_app->removeEditedColumns(items);
    refresh();
}

void DataEditorDialog::deleteAll() {
    QVector<QPair<int, QString>> items;
    for (int r = 0; r < m_editedList->count(); ++r) {
        auto *it = m_editedList->item(r);
        items.push_back({it->data(Qt::UserRole).toInt(), it->data(Qt::UserRole + 1).toString()});
    }
    if (items.isEmpty()) return;
    if (QMessageBox::question(this, "Delete all edited data", "Delete all edited data?") != QMessageBox::Yes) return;
    m_app->removeEditedColumns(items);
    refresh();
}
