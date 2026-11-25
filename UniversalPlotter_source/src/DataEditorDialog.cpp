#include "DataEditorDialog.h"
#include "MainWindow.h"
#include "ExprEval.h"
#include "ExprAst.h"

#include <QComboBox>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTabBar>
#include <QScrollArea>
#include <QSizePolicy>
#include <QFormLayout>
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
#include <vector>

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
    resize(600, 780);

    // Everything sits in a scroll area, so the window can be made smaller than its contents (small monitors).
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    auto *hint = new QLabel(
        "Choose a dataset, its X column and the Y column(s), then press an operation or type a math expression. "
        "The result appears in the plot window's data selector as a new column with the operation added to its name. "
        "The Noise removal tab has smoothing and filtering methods with their parameters. "
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
    m_yList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);        // takes the free room, its default size hint does not count
    dl->addWidget(m_yList, 1);
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

    // Tabs: a tab bar above the pages. (A QTabWidget would be as high as its highest page; here the area is only as
    // high as the page that is shown, so the lists below get the free room.)
    auto *tabBar = new QTabBar;
    tabBar->setExpanding(false);
    tabBar->addTab("Basic operations");
    tabBar->addTab("Math expression");
    tabBar->addTab("Noise removal");
    auto *tabHost = new QWidget;                        // only the page of the chosen tab is shown, so the area is as high as that page
    tabHost->setObjectName("tabHost");
    tabHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *tabHostLayout = new QVBoxLayout(tabHost);
    tabHostLayout->setContentsMargins(0, 0, 0, 0);
    const QVector<QWidget *> tabPages = {opBox, exprBox, buildNoiseTab()};
    for (int i = 0; i < tabPages.size(); ++i) {
        tabHostLayout->addWidget(tabPages[i]);
        tabPages[i]->setVisible(i == 0);
    }
    connect(tabBar, &QTabBar::currentChanged, this, [tabPages, tabHost](int idx) {
        for (int i = 0; i < tabPages.size(); ++i) tabPages[i]->setVisible(i == idx);
        tabHost->updateGeometry();
    });
    auto *tabArea = new QVBoxLayout;
    tabArea->setSpacing(2);
    tabArea->addWidget(tabBar);
    tabArea->addWidget(tabHost);
    layout->addLayout(tabArea);
    connect(exprBtn, &QPushButton::clicked, this, [this]() { apply(Op::Expr); });
    connect(m_exprEdit, &QLineEdit::returnPressed, this, [this]() { apply(Op::Expr); });

    // ---- edited data ----
    auto *editedBox = new QGroupBox("Edited data (also shown in the plot window)");
    auto *el = new QVBoxLayout(editedBox);
    m_editedList = new QListWidget;
    m_editedList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_editedList->setMinimumHeight(120);
    m_editedList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
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
    connect(m_yList, &QListWidget::itemSelectionChanged, this, &DataEditorDialog::updateSamplingInfo);
    connect(delBtn, &QPushButton::clicked, this, &DataEditorDialog::deleteSelected);
    connect(delAllBtn, &QPushButton::clicked, this, &DataEditorDialog::deleteAll);

    refresh();
}

// ---------------------------------------------------------------------------------------------------------------
// "Noise removal" tab: a method selector and one page of parameters per method (the pages follow nf::Method).
// ---------------------------------------------------------------------------------------------------------------
QWidget *DataEditorDialog::buildNoiseTab() {
    NoiseUi &u = m_nu;
    auto *tab = new QGroupBox("Noise removal on the selected Y column(s)");
    auto *tl = new QVBoxLayout(tab);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel("Method:"));
    u.method = new QComboBox;
    u.method->addItems({
        "Moving average (uniform / weighted)", "Gaussian filter", "Exponential smoothing (EMA)",
        "Holt double exponential (level + trend)", "Savitzky-Golay filter", "Median filter",
        "Hampel filter (spike removal)", "Outlier removal (global)", "LOWESS / LOESS",
        "Whittaker-Eilers smoother", "Total variation denoising", "Bilateral filter (edge preserving)",
        "Adaptive Wiener filter", "Kalman filter / smoother", "Butterworth filter (low / high / band)",
        "FFT filter (frequency domain)", "Wavelet shrinkage"});
    u.method->setToolTip("Choose the noise removal method; its parameters are shown below.");
    top->addWidget(u.method, 1);
    tl->addLayout(top);

    u.info = new QLabel;
    u.info->setStyleSheet("color:#555;");
    u.info->setWordWrap(true);
    tl->addWidget(u.info);

    u.pages = new QWidget;
    u.pages->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *host = new QVBoxLayout(u.pages);
    host->setContentsMargins(0, 0, 0, 0);
    tl->addWidget(u.pages);

    // ---- small widget factories ----
    QFormLayout *form = nullptr;
    auto newPage = [&](const QString &hint) {
        auto *page = new QWidget;
        auto *pl = new QVBoxLayout(page);
        pl->setContentsMargins(0, 4, 0, 0);
        auto *h = new QLabel(hint);
        h->setWordWrap(true);
        h->setStyleSheet("color:#777;");
        pl->addWidget(h);
        form = new QFormLayout;
        pl->addLayout(form);
        pl->addStretch();
        host->addWidget(page);
        page->setVisible(u.pageList.isEmpty());      // (only the first page is shown at the start)
        u.pageList << page;
    };
    auto ispin = [&](const QString &label, int lo, int hi, int val, const QString &tip, int step = 1) {
        auto *s = new QSpinBox;
        s->setRange(lo, hi);
        s->setValue(val);
        s->setSingleStep(step);
        s->setToolTip(tip);
        form->addRow(label, s);
        return s;
    };
    auto dspin = [&](const QString &label, double lo, double hi, double val, int dec, const QString &tip) {
        auto *s = new QDoubleSpinBox;
        s->setDecimals(dec);
        s->setRange(lo, hi);
        s->setValue(val);
        s->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        s->setToolTip(tip);
        form->addRow(label, s);
        return s;
    };
    auto combo = [&](const QString &label, const QStringList &items, int cur, const QString &tip) {
        auto *c = new QComboBox;
        c->addItems(items);
        c->setCurrentIndex(cur);
        c->setToolTip(tip);
        form->addRow(label, c);
        return c;
    };
    auto showRow = [](QFormLayout *f, QWidget *w, bool on) {      // hides / shows a field together with its label
        w->setVisible(on);
        if (QWidget *l = f->labelForField(w)) l->setVisible(on);
    };
    const QString windowTip = "Number of points in the window (made odd: 6 -> 7). Larger = smoother.";
    const QStringList edgeItems = {"Mirror the data", "Repeat the edge value", "Shrink the window"};
    const QString edgeTip = "What the window does at the first and last points:\n"
                            "Mirror = reflect the data at the edge, Repeat = copy the edge value,\n"
                            "Shrink = use only the points that exist.";
    // cut-off boxes with a unit selector (fraction of the Nyquist frequency, or cycles per X unit)
    auto cutoffs = [&](QComboBox *&unit, QDoubleSpinBox *&c1, QDoubleSpinBox *&c2, double d1, double d2, QLabel *&lab2) {
        unit = combo("Cut-off unit:", {"Fraction of Nyquist (0 - 1)", "Frequency (cycles per X unit)"}, 0,
                     "Nyquist frequency = half the sampling rate = the highest frequency the data can hold.\n"
                     "The status line above shows its value for the selected data.");
        c1 = dspin("Cut-off:", 0.0001, 0.9999, d1, 4, "Cut-off frequency (the lower edge for band filters).");
        c2 = dspin("Upper cut-off:", 0.0001, 0.9999, d2, 4, "Upper edge of the band (band-pass / band-stop only).");
        lab2 = qobject_cast<QLabel *>(form->labelForField(c2));
        QComboBox *ucb = unit;
        QDoubleSpinBox *a = c1, *b = c2;
        connect(ucb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, a, b](int idx) {
            const bool freq = idx == 1;
            for (QDoubleSpinBox *s : {a, b}) {
                const double v = s->value();
                s->blockSignals(true);
                if (freq) { s->setRange(0, 1e12); s->setDecimals(6); s->setValue(m_nyquist > 0 ? v * m_nyquist : v); }
                else { s->setRange(0.0001, 0.9999); s->setDecimals(4); s->setValue(m_nyquist > 0 ? std::min(0.9999, v / m_nyquist) : std::min(v, 0.9999)); }
                s->blockSignals(false);
            }
        });
    };

    // 0 Moving average
    newPage("Replaces every point by the average of the points around it. Weighted windows count the points near the centre more.");
    u.maWindow = ispin("Window (points):", 1, 100001, 5, windowTip, 2);
    u.maWeights = combo("Weights:", {"Uniform (simple average)", "Triangular", "Hann", "Hamming", "Blackman"}, 0,
                        "Shape of the window. Uniform = plain average; the others reduce ripples in the result.");
    u.maEdge = combo("Edges:", edgeItems, 0, edgeTip);
    // 1 Gaussian
    newPage("Smooths with a bell-shaped kernel (Gaussian blur). The window is 4 sigma wide on each side.");
    u.gaSigma = dspin("Sigma (points):", 0.1, 10000, 2.0, 2, "Standard deviation of the kernel in points. Larger = smoother.");
    u.gaEdge = combo("Edges:", edgeItems, 0, edgeTip);
    // 2 Exponential
    newPage("Exponentially weighted moving average: every value is a mix of the new point and the previous result.");
    u.emaAlpha = dspin("Alpha:", 0.001, 1.0, 0.3, 3, "Smoothing factor: small = smooth (and slow), 1 = no smoothing.");
    u.emaDir = combo("Direction:", {"Forward (causal, lags behind)", "Backward", "Forward + backward (no lag)"}, 2,
                     "Forward follows the data with a delay. Forward + backward removes the delay (zero phase) but smooths twice as much.");
    // 3 Holt
    newPage("Double exponential smoothing: follows a level and a trend, so it lags less on slopes than plain EMA (forward in X).");
    u.holtAlpha = dspin("Alpha (level):", 0.001, 1.0, 0.3, 3, "Smoothing of the level. Small = smoother.");
    u.holtBeta = dspin("Beta (trend):", 0.0, 1.0, 0.1, 3, "Smoothing of the trend (slope). Small = steadier slope.");
    // 4 Savitzky-Golay
    newPage("Fits a polynomial to the points in a sliding window. Keeps peaks and their height much better than a moving average. Uses the real X values, so uneven spacing is fine.");
    u.sgWindow = ispin("Window (points):", 3, 100001, 11, windowTip, 2);
    u.sgOrder = ispin("Polynomial order:", 0, 10, 3, "Degree of the fitted polynomial. Must be smaller than the window. 2-4 is typical; higher keeps more detail (and more noise).");
    // 5 Median
    newPage("Replaces every point by the median of its window. Removes isolated spikes and keeps steps sharp.");
    u.medWindow = ispin("Window (points):", 1, 100001, 5, windowTip, 2);
    u.medEdge = combo("Edges:", edgeItems, 0, edgeTip);
    // 6 Hampel
    newPage("Finds spikes with a rolling median and median absolute deviation, and replaces only those points by the median. All other points stay exactly as they are.");
    u.hampWindow = ispin("Window (points):", 3, 100001, 7, windowTip, 2);
    u.hampSigmas = dspin("Threshold (sigmas):", 0.1, 100, 3.0, 2, "A point further than this many (MAD based) standard deviations from the window median is a spike. 3 is the usual value; smaller removes more.");
    u.hampEdge = combo("Edges:", edgeItems, 0, edgeTip);
    // 7 Outlier
    newPage("Finds points that are far from the rest of the whole column and replaces them. Other points are not changed.");
    u.outRule = combo("Rule:", {"Z-score (mean, standard deviation)", "Modified Z-score (median, MAD)", "IQR (interquartile range)"}, 1,
                      "Z-score: distance from the mean in standard deviations.\nModified Z-score: robust version with median and MAD (best default).\nIQR: beyond Q1 - k*IQR or Q3 + k*IQR (box plot whiskers).");
    u.outThreshold = dspin("Threshold:", 0.1, 1000, 3.5, 2, "Z-score: usually 3. Modified Z-score: usually 3.5. IQR factor: usually 1.5 (3 = extreme outliers only).");
    u.outReplace = combo("Replace with:", {"Linear interpolation", "Empty value (gap)", "Nearest limit (clip)"}, 0,
                         "Interpolation fills the gap from the neighbours; Empty leaves a gap in the curve; Clip moves the point to the limit.");
    connect(u.outRule, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        const double def[3] = {3.0, 3.5, 1.5};
        m_nu.outThreshold->setValue(def[std::min(std::max(i, 0), 2)]);
    });
    // 8 LOWESS
    newPage("Locally weighted regression: for every point a weighted straight line (or parabola) is fitted to the nearest points. Robust iterations make it ignore outliers. Uses the real X values.");
    u.lowFraction = dspin("Fraction of points:", 0.001, 1.0, 0.1, 3, "Share of all points used for every local fit. Larger = smoother. (Slow for very large data with a large fraction.)");
    u.lowDegree = combo("Local polynomial:", {"Constant (0)", "Linear (1)", "Quadratic (2)"}, 1, "Degree of the local fit. Linear is the classic LOWESS; quadratic follows curves and peaks better (LOESS).");
    u.lowRobust = ispin("Robust iterations:", 0, 10, 2, "0 = plain fit. 2-3 down-weights outliers (classic LOWESS uses 3).");
    // 9 Whittaker
    newPage("Penalised least squares smoother: balances closeness to the data against roughness. One setting (lambda) controls everything; assumes evenly spaced points.");
    u.whLambda = dspin("Lambda (smoothness):", 0.0, 1e12, 100.0, 3, "Larger = smoother. Try 10, 100, 1000, ... (the spin arrows scale with the value).");
    u.whOrder = ispin("Difference order:", 1, 6, 2, "Which roughness is penalised: 1 = slopes (result tends to flat), 2 = curvature (result tends to a straight line, most common), 3 = ...");
    // 10 Total variation
    newPage("Total variation (ROF) denoising: removes noise but keeps steps and sharp edges, giving piecewise flat results. Assumes evenly spaced points.");
    u.tvLambda = dspin("Lambda (strength):", 0.0, 1e12, 1.0, 4, "Strength in the units of Y: roughly the size of the noise you want removed, times a few. Larger = flatter.");
    u.tvIters = ispin("Iterations:", 1, 100000, 500, "More iterations = closer to the exact solution (slower). 200-2000 is usually enough.", 100);
    // 11 Bilateral
    newPage("Averages neighbours that are close in position AND close in value, so edges and steps are not blurred.");
    u.bilWindow = ispin("Window (points):", 3, 100001, 9, windowTip, 2);
    u.bilSigmaS = dspin("Sigma space (points):", 0.1, 10000, 3.0, 2, "How fast the weight falls off with the distance in points.");
    u.bilSigmaR = dspin("Sigma range (Y units):", 0.0, 1e12, 0.0, 6, "Values differing by more than about this are not mixed. 0 = automatic (estimated from the noise).");
    u.bilEdge = combo("Edges:", edgeItems, 0, edgeTip);
    // 12 Wiener
    newPage("Adaptive Wiener filter: smooths strongly where the data is as variable as the noise, and hardly at all where there is real structure.");
    u.wieWindow = ispin("Window (points):", 3, 100001, 7, windowTip, 2);
    u.wieNoise = dspin("Noise variance:", 0.0, 1e12, 0.0, 8, "Variance (sigma squared) of the noise, in Y units squared. 0 = automatic (mean of the local variances).");
    u.wieEdge = combo("Edges:", edgeItems, 0, edgeTip);
    // 13 Kalman
    newPage("Kalman filter with a drifting-level model. The only setting is how much the true value is allowed to change compared with the measurement noise.");
    u.kalQ = dspin("Process / measurement noise:", 1e-9, 1e6, 0.05, 6, "Q/R ratio. Small = trust the model (very smooth), large = trust the measurements (follows the data).");
    u.kalSmooth = new QCheckBox("Smoother (also run backward, no lag)");
    u.kalSmooth->setChecked(true);
    u.kalSmooth->setToolTip("Adds the Rauch-Tung-Striebel backward pass. Without it the result is the real-time filter, which lags behind the data.");
    form->addRow(u.kalSmooth);
    // 14 Butterworth
    {
        newPage("Classic maximally flat IIR filter. Low-pass removes fast noise, high-pass removes slow drift, band-pass keeps a band, band-stop removes a band (e.g. 50 Hz hum). Uneven X is resampled evenly first.");
        u.bwType = combo("Type:", {"Low-pass", "High-pass", "Band-pass", "Band-stop (notch)"}, 0, "Which frequencies are kept.");
        u.bwOrder = ispin("Order:", 1, 12, 4, "Steepness of the cut-off: higher = sharper (but more ringing). 2-6 is typical.");
        QLabel *lab2 = nullptr;
        cutoffs(u.bwUnit, u.bwC1, u.bwC2, 0.1, 0.3, lab2);
        u.bwZero = new QCheckBox("Zero-phase (forward + backward, no shift)");
        u.bwZero->setChecked(true);
        u.bwZero->setToolTip("Filters forward and backward so peaks are not shifted. The effective order is doubled.\nUntick for the one-pass causal filter (adds a delay).");
        form->addRow(u.bwZero);
        QFormLayout *f = form;
        (void)lab2;
        auto sync = [this, f, showRow]() {
            showRow(f, m_nu.bwC2, m_nu.bwType->currentIndex() >= 2);      // upper cut-off: band filters only
        };
        connect(u.bwType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, sync);
        sync();
    }
    // 15 FFT
    {
        newPage("Removes frequencies in the Fourier spectrum (the ends are mirrored to avoid edge artefacts). A sharp cut-off can ring; use Butterworth for gentler filtering. Uneven X is resampled evenly first.");
        u.fftMode = combo("Mode:", {"Low-pass", "High-pass", "Band-pass", "Band-stop (notch)", "Keep strongest components only"}, 0,
                          "Keep strongest: removes every frequency whose amplitude is below a percentage of the strongest one (a spectral threshold).");
        QLabel *lab2 = nullptr;
        cutoffs(u.fftUnit, u.fftC1, u.fftC2, 0.1, 0.3, lab2);
        u.fftKeep = dspin("Keep above (% of strongest):", 0.0, 100.0, 10.0, 2, "Frequencies weaker than this share of the strongest component are removed.");
        QLabel *labKeep = qobject_cast<QLabel *>(form->labelForField(u.fftKeep));
        QFormLayout *f = form;
        (void)lab2; (void)labKeep;
        auto sync = [this, f, showRow]() {
            const int mode = m_nu.fftMode->currentIndex();
            const bool band = mode == 2 || mode == 3, keep = mode == 4;
            showRow(f, m_nu.fftUnit, !keep);
            showRow(f, m_nu.fftC1, !keep);
            showRow(f, m_nu.fftC2, band);
            showRow(f, m_nu.fftKeep, keep);
        };
        connect(u.fftMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, sync);
        sync();
    }
    // 16 Wavelet
    newPage("Wavelet shrinkage: splits the data into scales, shrinks the small (noise) coefficients and rebuilds. Good for noisy signals with peaks, steps or changing detail. Assumes evenly spaced points.");
    u.wavFamily = combo("Wavelet:", QStringList(), 3, "Daubechies N has N vanishing moments: higher = smoother wavelet. Haar is blocky (good for steps).");
    for (const std::string &name : nf::waveletNames()) u.wavFamily->addItem(QString::fromStdString(name));
    u.wavFamily->setCurrentIndex(3);
    u.wavLevels = ispin("Levels:", 0, 20, 0, "Number of decomposition levels. 0 = as many as the number of points allows.");
    u.wavRule = combo("Threshold rule:", {"Universal (VisuShrink, automatic)", "Manual value"}, 0,
                      "Universal: sigma * sqrt(2 ln N), with the noise sigma estimated from the finest scale.");
    u.wavThreshold = dspin("Threshold / multiplier:", 0.0, 1e12, 1.0, 4,
                           "Universal: multiplies the automatic threshold (below 1 keeps more detail).\nManual: the threshold itself, in Y units.");
    u.wavType = combo("Shrinkage:", {"Soft (smoother)", "Hard (keeps peak heights)"}, 0, "Soft pulls surviving coefficients towards zero; hard keeps them unchanged.");

    // ---- buttons ----
    auto *bl = new QHBoxLayout;
    auto *applyBtn = new QPushButton("Apply noise removal");
    applyBtn->setToolTip("Remove noise from the selected Y column(s)  ->  name (method, parameters)\n"
                      "The original column is kept; the result is added to the \"(edited)\" dataset.");
    bl->addStretch();
    bl->addWidget(applyBtn);
    tl->addLayout(bl);
    connect(applyBtn, &QPushButton::clicked, this, [this]() { apply(Op::Denoise); });
    connect(u.method, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        for (int k = 0; k < m_nu.pageList.size(); ++k) m_nu.pageList[k]->setVisible(k == idx);
        m_nu.pages->updateGeometry();                   // the parameter area is as high as the page of this method
    });
    return tab;
}

nf::Params DataEditorDialog::readNoiseParams() const {
    const NoiseUi &u = m_nu;
    nf::Params p;
    p.method = nf::Method(u.method->currentIndex());
    auto edge = [](const QComboBox *c) { return nf::Edge(c->currentIndex()); };
    switch (p.method) {
    case nf::Method::MovingAverage: p.window = u.maWindow->value(); p.weights = nf::Window(u.maWeights->currentIndex()); p.edge = edge(u.maEdge); break;
    case nf::Method::Gaussian: p.sigma = u.gaSigma->value(); p.edge = edge(u.gaEdge); break;
    case nf::Method::Exponential: p.alpha = u.emaAlpha->value(); p.direction = nf::Direction(u.emaDir->currentIndex()); break;
    case nf::Method::Holt: p.alpha = u.holtAlpha->value(); p.beta = u.holtBeta->value(); break;
    case nf::Method::SavitzkyGolay: p.window = u.sgWindow->value(); p.polyOrder = u.sgOrder->value(); break;
    case nf::Method::Median: p.window = u.medWindow->value(); p.edge = edge(u.medEdge); break;
    case nf::Method::Hampel: p.window = u.hampWindow->value(); p.nSigmas = u.hampSigmas->value(); p.edge = edge(u.hampEdge); break;
    case nf::Method::Outlier: p.outlierRule = nf::OutlierRule(u.outRule->currentIndex()); p.threshold = u.outThreshold->value(); p.replace = nf::Replace(u.outReplace->currentIndex()); break;
    case nf::Method::Lowess: p.fraction = u.lowFraction->value(); p.polyOrder = u.lowDegree->currentIndex(); p.robustIterations = u.lowRobust->value(); break;
    case nf::Method::Whittaker: p.lambda = u.whLambda->value(); p.diffOrder = u.whOrder->value(); break;
    case nf::Method::TotalVariation: p.lambda = u.tvLambda->value(); p.iterations = u.tvIters->value(); break;
    case nf::Method::Bilateral: p.window = u.bilWindow->value(); p.sigmaSpace = u.bilSigmaS->value(); p.sigmaRange = u.bilSigmaR->value(); p.edge = edge(u.bilEdge); break;
    case nf::Method::Wiener: p.window = u.wieWindow->value(); p.noiseVariance = u.wieNoise->value(); p.edge = edge(u.wieEdge); break;
    case nf::Method::Kalman: p.qRatio = u.kalQ->value(); p.smoother = u.kalSmooth->isChecked(); break;
    case nf::Method::Butterworth:
        p.pass = nf::PassType(u.bwType->currentIndex()); p.order = u.bwOrder->value(); p.zeroPhase = u.bwZero->isChecked();
        p.cutoffUnit = nf::CutoffUnit(u.bwUnit->currentIndex()); p.cutoff1 = u.bwC1->value(); p.cutoff2 = u.bwC2->value();
        break;
    case nf::Method::Fft:
        p.fftMode = nf::FftMode(u.fftMode->currentIndex()); p.cutoffUnit = nf::CutoffUnit(u.fftUnit->currentIndex());
        p.cutoff1 = u.fftC1->value(); p.cutoff2 = u.fftC2->value(); p.keepPercent = u.fftKeep->value();
        break;
    case nf::Method::Wavelet:
        p.waveletIndex = u.wavFamily->currentIndex(); p.levels = u.wavLevels->value();
        p.thresholdRule = nf::ThresholdRule(u.wavRule->currentIndex()); p.thresholdValue = u.wavThreshold->value();
        p.thresholdType = nf::Threshold(u.wavType->currentIndex());
        break;
    }
    return p;
}

// Shows how many valid points the selected data has, its spacing and the Nyquist frequency (needed for the filter cut-offs).
void DataEditorDialog::updateSamplingInfo() {
    if (!m_nu.info) return;
    m_nyquist = 0;
    const int dsIdx = m_dsCombo->currentData().toInt();
    const QVector<Dataset> &all = m_app->datasets();
    if (m_dsCombo->count() == 0 || dsIdx < 0 || dsIdx >= all.size()) { m_nu.info->setText("No data selected."); return; }
    const Dataset &ds = all[dsIdx];
    const int n = rowCount(ds);
    QVector<double> x(n);
    const QString xName = m_xCombo->currentText();
    if (xName.isEmpty() || xName == indexLabel()) {
        for (int i = 0; i < n; ++i) x[i] = i;
    } else {
        const int xi = ds.columnIndex(xName);
        if (xi < 0) { m_nu.info->setText(""); return; }
        x = ds.columns[xi].values;
    }
    QString yName;
    const auto sel = m_yList->selectedItems();
    if (!sel.isEmpty()) yName = sel.first()->text();
    else if (m_yList->count() > 0) yName = m_yList->item(0)->text();
    const int yi = ds.columnIndex(yName);
    if (yi < 0) { m_nu.info->setText(""); return; }
    const QVector<double> &yv = ds.columns[yi].values;
    const std::vector<double> sx(x.begin(), x.end()), sy(yv.begin(), yv.end());
    const nf::SamplingInfo s = nf::samplingInfo(sx, sy);
    if (s.points < 2 || !(s.dx > 0)) { m_nu.info->setText(QString("\"%1\": too few valid points.").arg(yName)); return; }
    m_nyquist = 0.5 / s.dx;
    m_nu.info->setText(QString("\"%1\": %2 points, mean spacing %3, Nyquist frequency %4 per X unit%5.")
                           .arg(yName).arg(s.points).arg(s.dx, 0, 'g', 5).arg(m_nyquist, 0, 'g', 5)
                           .arg(s.even ? QString() : QString(" (uneven spacing: Butterworth / FFT resample it evenly)")));
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
    fillYListItems();
    updateSamplingInfo();
}

void DataEditorDialog::fillYListItems() {
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
    // internal kinds: 0 derivative, 2 integral, 3 abs, 4 sign flip, 5 math expression, 6 noise removal
    // (1 = second derivative is only used inside computeOrder)
    static const int kind[6] = {0, 2, 3, 4, 5, 6};
    const int op = kind[int(opEnum)];
    QString userExpr, fullExpr;
    nf::Params noise;
    if (op == 6) noise = readNoiseParams();
    if (op == 5) {
        QString err;
        if (!fullExpression(m_exprEdit->text(), fullExpr, err)) {
            QMessageBox::information(this, "Math expression", err);
            return;
        }
        userExpr = m_exprEdit->text().trimmed();
    }
    const int order = (op == 0 || op == 2) ? m_orderSpin->value() : 1;   // (abs, sign flip, expression, noise removal: 1)
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
        case 6: return QString(" (%1)").arg(QString::fromStdString(nf::describe(noise)));
        default: return QString(" (sign flipped)");
        }
    };
    const QString suffix = suffixFor();

    // A plotted function is edited as MATH: the result is a new formula (-sin(x), sin(x)*2, cos(x), ...) that is
    // plotted, fitted and exported to LaTeX like any typed function. Only an integral / derivative that cannot
    // be found as a formula (e.g. x*sin(x)) falls through to sampled points below.
    QString noFormulaNote;
    if (src.isFunction && op != 6) {      // (noise removal on a function gives sampled points, see below)
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
        } else if (op == 6) {                                // noise removal (NoiseFilter.cpp)
            const QVector<double> &yv = src.columns[yi].values;
            const std::vector<double> sx(x.begin(), x.end()), sy(yv.begin(), yv.end());
            std::vector<double> so;
            std::string e;
            if (!nf::filter(sx, sy, noise, so, e)) { failed << yName + ": " + QString::fromStdString(e); continue; }
            res = QVector<double>(n, kNaN);
            for (int i = 0; i < n && i < int(so.size()); ++i) res[i] = so[i];
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
