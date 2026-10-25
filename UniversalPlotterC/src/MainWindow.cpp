#include "MainWindow.h"
#include "DataLoader.h"
#include "LatexExport.h"
#include "LatexExportDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QCheckBox>
#include <QTextEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QFileDialog>
#include <QSettings>
#include "DataEntryDialog.h"
#include "ExprEval.h"
#include <QFontMetrics>
#include <QTimer>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QMouseEvent>
#include <QTextStream>
#include <QFile>
#include <QPixmap>
#include <QPainter>
#include <cmath>
#include <algorithm>

// ============================================================ construction
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Universal Data Plotter");
    m_plotColorCycle = fileColors();

    QSettings settings;
    m_lastOpenDir = settings.value("dirs/lastOpenDir").toString();
    m_lastSaveDir = settings.value("dirs/lastSaveDir").toString();

    buildUi();
}

void MainWindow::setLastOpenDir(const QString &dir) {
    m_lastOpenDir = dir;
    QSettings settings;
    settings.setValue("dirs/lastOpenDir", dir);
}

void MainWindow::setLastSaveDir(const QString &dir) {
    m_lastSaveDir = dir;
    QSettings settings;
    settings.setValue("dirs/lastSaveDir", dir);
}

void MainWindow::buildUi() {
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *outerLayout = new QVBoxLayout(central);

    // ---- top bar ----
    auto *topBar = new QWidget;
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(4, 4, 4, 4);
    topLayout->addWidget(new QLabel("Plot type:"));
    m_plotTypeCombo = new QComboBox;
    m_plotTypeCombo->addItems({"Line / scatter", "Bar", "Histogram"});
    m_plotTypeCombo->setToolTip("Line / scatter: curves (with fit, integral, cursor)\n"
                                "Bar: grouped bars per X value or category\n"
                                "Histogram: distribution of the selected column(s)");
    topLayout->addWidget(m_plotTypeCombo);
    auto *openBtn = new QPushButton("Open CSV / TXT...");
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openFiles);
    topLayout->addWidget(openBtn);
    auto *manualBtn = new QPushButton("Enter data manually...");
    manualBtn->setToolTip("Type or paste data (e.g. from Excel) into a table");
    connect(manualBtn, &QPushButton::clicked, this, &MainWindow::openDataEntry);
    topLayout->addWidget(manualBtn);
    m_fileLabel = new QLabel("No file loaded");
    m_fileLabel->setStyleSheet("color:#555;");
    topLayout->addWidget(m_fileLabel);
    topLayout->addStretch();
    outerLayout->addWidget(topBar);

    // ---- main splitter: left scrollable controls / right plot ----
    auto *splitter = new QSplitter(Qt::Horizontal);
    outerLayout->addWidget(splitter, 1);

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(300);
    scrollArea->setMaximumWidth(420);
    auto *leftBody = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftBody);
    leftLayout->setContentsMargins(8, 8, 8, 8);

    leftLayout->addWidget(buildParsingBox());
    leftLayout->addWidget(buildColumnsBox());
    m_funcBox = buildFunctionsBox();
    leftLayout->addWidget(m_funcBox);
    leftLayout->addWidget(buildPlotOptionsBox());
    m_fitBox = buildFitBox();
    m_integralBox = buildIntegralBox();
    m_interpBox = buildInterpBox();
    leftLayout->addWidget(m_fitBox);
    leftLayout->addWidget(m_integralBox);
    leftLayout->addWidget(m_interpBox);
    leftLayout->addWidget(buildAxisLimitsBox());
    leftLayout->addWidget(buildExportBox());
    leftLayout->addStretch();

    scrollArea->setWidget(leftBody);
    splitter->addWidget(scrollArea);

    // ---- right: plot + bottom action bar ----
    auto *rightContainer = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_plot = new QCustomPlot;
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    // The second Y axis (hidden until used) pans and zooms together with the first.
    m_plot->axisRect()->setRangeDragAxes(QList<QCPAxis *>{m_plot->xAxis, m_plot->yAxis, m_plot->yAxis2, m_plot->xAxis2});
    m_plot->axisRect()->setRangeZoomAxes(QList<QCPAxis *>{m_plot->xAxis, m_plot->yAxis, m_plot->yAxis2, m_plot->xAxis2});
    m_plot->xAxis2->setVisible(false);
    m_plot->xAxis2->setTickLabels(true);
    m_plot->xAxis2->grid()->setVisible(false);
    m_plot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    m_plot->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
    m_plot->yAxis2->setVisible(false);
    m_plot->yAxis2->setTickLabels(true);
    m_plot->yAxis2->grid()->setVisible(false);
    // Performance for large datasets: use QCustomPlot's faster (slightly
    // lower-fidelity) polyline routine; combined with the per-curve
    // antialiasing-off/marker-thinning done in plot() for big curves, this
    // keeps panning/zooming responsive. (We deliberately do NOT toggle
    // antialiasing on/off during drag via setNoAntialiasingOnDrag: that
    // causes a visible "pop" when the mouse is released and QCustomPlot
    // re-renders at full quality -- the fix above already covers the
    // actual bottleneck, so the toggle isn't needed.)
    m_plot->setPlottingHint(QCP::phFastPolylines, true);
    m_plot->legend->setVisible(false);
    m_plot->xAxis->setLabel("");
    m_plot->yAxis->setLabel("");
    m_plot->plotLayout()->insertRow(0);
    auto *titleElem = new QCPTextElement(m_plot, "No data plotted", QFont("sans", 11, QFont::Bold));
    m_plot->plotLayout()->addElement(0, 0, titleElem);
    m_plot->replot();

    m_historyDebounceTimer = new QTimer(this);
    m_historyDebounceTimer->setSingleShot(true);
    connect(m_historyDebounceTimer, &QTimer::timeout, this, &MainWindow::pushViewHistory);

    // Fires for ANY axis range change -- drag pan, wheel zoom, box zoom,
    // Autoscale, Apply limits, Home/Back/Forward -- so both the limit edit
    // boxes and the navigation history stay correct no matter how the view
    // changed (previously this only updated on mouse release, so wheel-zoom
    // never touched the edit boxes).
    auto onRangeChanged = [this](const QCPRange &) {
        syncAxisLimitEdits();
        if (!m_restoringView) m_historyDebounceTimer->start(250);
    };
    connect(m_plot->xAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged), this, onRangeChanged);
    connect(m_plot->yAxis, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged), this, onRangeChanged);
    connect(m_plot->yAxis2, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged), this, onRangeChanged);
    connect(m_plot->xAxis2, QOverload<const QCPRange &>::of(&QCPAxis::rangeChanged), this, onRangeChanged);

    // Box-zoom only knows about the first Y axis; zoom the second one to the same
    // screen rectangle. (Connected before the zoom is applied, so pixel->value is still valid.)
    connect(m_plot->selectionRect(), &QCPSelectionRect::accepted, this, [this](const QRect &rect, QMouseEvent *) {
        QCPAxis *second = secondValueAxis();
        if (!second->visible() || rect.height() < 3 || rect.width() < 3) return;
        double a, b;
        if (second->orientation() == Qt::Vertical) {
            a = second->pixelToCoord(rect.bottom());
            b = second->pixelToCoord(rect.top());
        } else {                                   // sideways bars: values run along the top axis
            a = second->pixelToCoord(rect.left());
            b = second->pixelToCoord(rect.right());
        }
        if (a > b) std::swap(a, b);
        if (a < b) second->setRange(a, b);
    });

    connect(m_plot, &QCustomPlot::mousePress, this, &MainWindow::onPlotMousePressed);

    rightLayout->addWidget(buildNavToolbar());
    rightLayout->addWidget(m_plot, 1);

    auto *bottomBar = new QWidget;
    auto *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(4, 4, 4, 4);
    auto *plotBtn = new QPushButton("Plot");
    connect(plotBtn, &QPushButton::clicked, this, &MainWindow::plot);
    bottomLayout->addWidget(plotBtn);
    auto *clearBtn = new QPushButton("Clear plot");
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearPlot);
    bottomLayout->addWidget(clearBtn);
    bottomLayout->addStretch();
    auto *latexBtn = new QPushButton("Export LaTeX (pgfplots)...");
    connect(latexBtn, &QPushButton::clicked, this, &MainWindow::exportLatex);
    bottomLayout->addWidget(latexBtn);
    rightLayout->addWidget(bottomBar);

    splitter->addWidget(rightContainer);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
}

// --------------------------------------------------------- navigation toolbar
QWidget *MainWindow::buildNavToolbar() {
    auto *bar = new QWidget;
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 2, 4, 4);

    m_homeBtn = new QPushButton("⌂ Home");
    m_homeBtn->setToolTip("Reset to the view the plot started at");
    connect(m_homeBtn, &QPushButton::clicked, this, &MainWindow::goHome);
    layout->addWidget(m_homeBtn);

    m_backBtn = new QPushButton("◀ Back");
    m_backBtn->setToolTip("Previous view");
    connect(m_backBtn, &QPushButton::clicked, this, &MainWindow::goBack);
    layout->addWidget(m_backBtn);

    m_forwardBtn = new QPushButton("Forward ▶");
    m_forwardBtn->setToolTip("Next view");
    connect(m_forwardBtn, &QPushButton::clicked, this, &MainWindow::goForward);
    layout->addWidget(m_forwardBtn);

    layout->addSpacing(16);

    m_panBtn = new QPushButton("✋ Pan");
    m_panBtn->setCheckable(true);
    m_panBtn->setChecked(true);
    m_panBtn->setToolTip("Drag to pan, scroll wheel to zoom");
    connect(m_panBtn, &QPushButton::clicked, this, &MainWindow::setPanMode);
    layout->addWidget(m_panBtn);

    m_zoomBtn = new QPushButton("🔍 Zoom");
    m_zoomBtn->setCheckable(true);
    m_zoomBtn->setToolTip("Drag a rectangle to zoom into it");
    connect(m_zoomBtn, &QPushButton::clicked, this, &MainWindow::setZoomRectMode);
    layout->addWidget(m_zoomBtn);

    layout->addStretch();

    updateNavButtonsEnabled();
    return bar;
}

void MainWindow::setPanMode() {
    m_panBtn->setChecked(true);
    m_zoomBtn->setChecked(false);
    m_plot->setSelectionRectMode(QCP::srmNone);
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
}

void MainWindow::setZoomRectMode() {
    m_zoomBtn->setChecked(true);
    m_panBtn->setChecked(false);
    // Drag now draws a rectangle to zoom into (QCustomPlot applies the zoom
    // itself once the drag is released); wheel-zoom stays available too.
    m_plot->setInteractions(QCP::iRangeZoom);
    m_plot->setSelectionRectMode(QCP::srmZoom);
}

void MainWindow::syncAxisLimitEdits() {
    m_xlimMinEdit->setText(QString::number(m_plot->xAxis->range().lower, 'g', 6));
    m_xlimMaxEdit->setText(QString::number(m_plot->xAxis->range().upper, 'g', 6));
    m_ylimMinEdit->setText(QString::number(m_plot->yAxis->range().lower, 'g', 6));
    m_ylimMaxEdit->setText(QString::number(m_plot->yAxis->range().upper, 'g', 6));
    if (m_y2limMinEdit) {
        m_y2limMinEdit->setText(QString::number(secondValueAxis()->range().lower, 'g', 6));
        m_y2limMaxEdit->setText(QString::number(secondValueAxis()->range().upper, 'g', 6));
    }
}

void MainWindow::resetViewHistory() {
    m_viewHistory.clear();
    m_viewHistory.append({m_plot->xAxis->range(), m_plot->yAxis->range(), secondValueAxis()->range()});
    m_viewHistoryPos = 0;
    updateNavButtonsEnabled();
}

void MainWindow::pushViewHistory() {
    if (m_restoringView || m_viewHistory.isEmpty()) return;
    QCPRange xr = m_plot->xAxis->range();
    QCPRange yr = m_plot->yAxis->range();
    QCPRange y2r = secondValueAxis()->range();
    const auto &top = m_viewHistory[m_viewHistoryPos];
    if (top == ViewState{xr, yr, y2r}) return; // no real change, e.g. debounce firing twice

    // dropping any "forward" entries once the user navigates to a new view
    while (m_viewHistory.size() > m_viewHistoryPos + 1) m_viewHistory.removeLast();
    m_viewHistory.append({xr, yr, y2r});
    // keep the history from growing without bound
    while (m_viewHistory.size() > 200) { m_viewHistory.removeFirst(); }
    m_viewHistoryPos = m_viewHistory.size() - 1;
    updateNavButtonsEnabled();
}

void MainWindow::restoreView(const ViewState &view) {
    m_restoringView = true;
    m_plot->xAxis->setRange(view.x);
    m_plot->yAxis->setRange(view.y);
    if (secondValueAxis()->visible()) secondValueAxis()->setRange(view.y2);
    m_plot->replot();
    syncAxisLimitEdits();
    m_restoringView = false;
}

void MainWindow::goHome() {
    if (m_viewHistory.isEmpty()) return;
    m_viewHistoryPos = 0;
    restoreView(m_viewHistory.first());
    updateNavButtonsEnabled();
}

void MainWindow::goBack() {
    if (m_viewHistoryPos <= 0) return;
    --m_viewHistoryPos;
    restoreView(m_viewHistory[m_viewHistoryPos]);
    updateNavButtonsEnabled();
}

void MainWindow::goForward() {
    if (m_viewHistoryPos < 0 || m_viewHistoryPos + 1 >= m_viewHistory.size()) return;
    ++m_viewHistoryPos;
    restoreView(m_viewHistory[m_viewHistoryPos]);
    updateNavButtonsEnabled();
}

void MainWindow::updateNavButtonsEnabled() {
    if (!m_homeBtn) return; // toolbar not built yet
    bool haveHistory = !m_viewHistory.isEmpty();
    m_homeBtn->setEnabled(haveHistory);
    m_backBtn->setEnabled(haveHistory && m_viewHistoryPos > 0);
    m_forwardBtn->setEnabled(haveHistory && m_viewHistoryPos + 1 < m_viewHistory.size());
}

void MainWindow::onPlotMousePressed(QMouseEvent *event) {
    if (m_fitPlacementActive) { onPlotClickedForFit(event); return; }
    if (m_integralPlacementActive) { onPlotClickedForIntegral(event); return; }
    if (m_interpPlacementActive) { onPlotClickedForInterp(event); return; }
}

// ---------------------------------------------------------------- parsing
QGroupBox *MainWindow::buildParsingBox() {
    auto *box = new QGroupBox("Parsing options");
    auto *grid = new QGridLayout(box);

    grid->addWidget(new QLabel("Separator:"), 0, 0);
    m_sepCombo = new QComboBox;
    m_sepCombo->addItems({"Auto", "Comma  ( , )", "Semicolon  ( ; )", "Tab", "Pipe  ( | )", "Whitespace"});
    m_sepCombo->setMaximumWidth(160);
    grid->addWidget(m_sepCombo, 0, 1);

    grid->addWidget(new QLabel("Decimal mark:"), 1, 0);
    m_decimalCombo = new QComboBox;
    m_decimalCombo->addItems({"Auto", "Dot  ( . )", "Comma  ( , )"});
    m_decimalCombo->setMaximumWidth(160);
    grid->addWidget(m_decimalCombo, 1, 1);

    auto *reparseBtn = new QPushButton("Re-parse file(s)");
    connect(reparseBtn, &QPushButton::clicked, this, &MainWindow::reparseFiles);
    grid->addWidget(reparseBtn, 2, 0, 1, 2);

    m_infoLabel = new QLabel;
    m_infoLabel->setStyleSheet("color:#555;");
    m_infoLabel->setWordWrap(true);
    grid->addWidget(m_infoLabel, 3, 0, 1, 2);

    return box;
}

// ---------------------------------------------------------------- columns
QGroupBox *MainWindow::buildColumnsBox() {
    auto *box = new QGroupBox("Columns");
    auto *layout = new QVBoxLayout(box);

    m_xRowsLabel = new QLabel("X axis (pick one per loaded file):");
    layout->addWidget(m_xRowsLabel);
    m_xRowsContainer = new QWidget;
    m_xRowsLayout = new QVBoxLayout(m_xRowsContainer);
    m_xRowsLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_xRowsContainer);

    layout->addWidget(new QLabel("Y axis (select one or more -- color = source file):"));
    m_yList = new QListWidget;
    m_yList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_yList->setMinimumHeight(140);
    layout->addWidget(m_yList);

    return box;
}

// -------------------------------------------------------------- functions
// A plotted function is stored as an ordinary Dataset (columns "x" and "f(x)", sampled on
// [min, max]) so it appears in the column selectors, plots, fits and exports like loaded data.
namespace {
// A number or a small constant expression ("2*pi"); returns false if it cannot be read.
bool parseNumberOrExpr(const QString &text, double &out) {
    QString t = text.trimmed();
    if (t.isEmpty()) return false;
    bool ok = false;
    double v = t.toDouble(&ok);
    if (!ok) v = t.replace(',', '.').toDouble(&ok);
    if (!ok) {
        try { v = ExprEval(text).evaluate({}); ok = true; } catch (...) { return false; }
    }
    if (!ok || !std::isfinite(v)) return false;
    out = v;
    return true;
}

Dataset makeFunctionDataset(const QString &expr, double lo, double hi, int n) {
    Dataset ds;
    ds.path = functionDataPath();
    ds.basename = expr;
    ds.isFunction = true;
    ds.funcExpr = expr;
    ds.funcMin = lo;
    ds.funcMax = hi;
    ds.funcSamples = n;
    ColumnData xc, yc;
    xc.name = "x";
    yc.name = "f(x)";
    ExprEval ev(expr);
    for (int i = 0; i < n; ++i) {
        double x = (n > 1) ? lo + (hi - lo) * i / (n - 1) : lo;
        double y = std::numeric_limits<double>::quiet_NaN();
        try {
            y = ev.evaluate({{"x", x}});
            if (!std::isfinite(y)) y = std::numeric_limits<double>::quiet_NaN();   // e.g. 1/0, ln(-1): left as a gap
        } catch (...) {}
        xc.values << x;
        yc.values << y;
    }
    ds.columns = { xc, yc };
    ds.xColumn = "x";
    ds.info.sep = ",";
    ds.info.decimal = ".";
    ds.info.hasHeader = true;
    ds.info.rows = n;
    ds.info.cols = 2;
    return ds;
}
} // namespace

QGroupBox *MainWindow::buildFunctionsBox() {
    auto *box = new QGroupBox("Functions");
    auto *layout = new QVBoxLayout(box);

    auto *exprRow = new QHBoxLayout;
    exprRow->addWidget(new QLabel("f(x) ="));
    m_funcExprEdit = new QLineEdit;
    m_funcExprEdit->setPlaceholderText("e.g. sin(x)/x");
    m_funcExprEdit->setToolTip("Uses x, numbers, + - * / ^, parentheses, pi, e and\\n"
                               "sin cos tan asin acos atan sinh cosh tanh exp log log10 log2 sqrt abs");
    exprRow->addWidget(m_funcExprEdit, 1);
    layout->addLayout(exprRow);

    auto *domRow = new QGridLayout;
    domRow->addWidget(new QLabel("Domain min:"), 0, 0);
    m_funcMinEdit = new QLineEdit("0");
    m_funcMinEdit->setToolTip("Start of the domain. A number or a constant such as 2*pi.");
    domRow->addWidget(m_funcMinEdit, 0, 1);
    domRow->addWidget(new QLabel("max:"), 0, 2);
    m_funcMaxEdit = new QLineEdit("10");
    m_funcMaxEdit->setToolTip("End of the domain. A number or a constant such as 2*pi.");
    domRow->addWidget(m_funcMaxEdit, 0, 3);
    domRow->addWidget(new QLabel("Samples:"), 1, 0);
    m_funcSamplesSpin = new QSpinBox;
    m_funcSamplesSpin->setRange(2, 200000);
    m_funcSamplesSpin->setValue(200);
    m_funcSamplesSpin->setToolTip("Number of points the function is evaluated at (evenly spaced over the domain)");
    domRow->addWidget(m_funcSamplesSpin, 1, 1);
    layout->addLayout(domRow);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton("Add");
    addBtn->setToolTip("Add the function above to the list and to the column selectors");
    m_funcUpdateBtn = new QPushButton("Update");
    m_funcUpdateBtn->setToolTip("Apply the fields above to the function selected in the list");
    m_funcRemoveBtn = new QPushButton("Remove");
    m_funcRemoveBtn->setToolTip("Delete the function selected in the list");
    m_funcUpdateBtn->setEnabled(false);
    m_funcRemoveBtn->setEnabled(false);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(m_funcUpdateBtn);
    btnRow->addWidget(m_funcRemoveBtn);
    layout->addLayout(btnRow);

    layout->addWidget(new QLabel("Functions (click one to see / edit its domain and samples):"));
    m_funcList = new QListWidget;
    m_funcList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_funcList->setMaximumHeight(110);
    layout->addWidget(m_funcList);

    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addFunction);
    connect(m_funcUpdateBtn, &QPushButton::clicked, this, &MainWindow::updateFunction);
    connect(m_funcRemoveBtn, &QPushButton::clicked, this, &MainWindow::removeFunction);
    connect(m_funcList, &QListWidget::itemSelectionChanged, this, &MainWindow::onFunctionSelected);
    connect(m_funcExprEdit, &QLineEdit::returnPressed, this, [this] {
        if (m_funcList->currentRow() >= 0 && m_funcList->selectedItems().size()) updateFunction(); else addFunction();
    });
    return box;
}

QVector<int> MainWindow::functionDatasetIndices() const {
    QVector<int> idx;
    for (int i = 0; i < m_datasets.size(); ++i) if (m_datasets[i].isFunction) idx << i;
    return idx;
}

// Reads and checks the fields; shows a message and returns false when something is wrong.
bool MainWindow::readFunctionInputs(QString &expr, double &lo, double &hi, int &n) {
    expr = m_funcExprEdit->text().trimmed();
    if (expr.isEmpty()) {
        QMessageBox::warning(this, "Function", "Enter a function f(x).");
        return false;
    }
    if (!parseNumberOrExpr(m_funcMinEdit->text(), lo)) {
        QMessageBox::warning(this, "Function", "The domain minimum is not a valid number.");
        return false;
    }
    if (!parseNumberOrExpr(m_funcMaxEdit->text(), hi)) {
        QMessageBox::warning(this, "Function", "The domain maximum is not a valid number.");
        return false;
    }
    if (!(lo < hi)) {
        QMessageBox::warning(this, "Function", "The domain minimum must be smaller than the maximum.");
        return false;
    }
    n = m_funcSamplesSpin->value();
    try {   // catches syntax errors / unknown names before anything is added
        ExprEval(expr).evaluate({{"x", 0.5 * (lo + hi)}});
    } catch (std::exception &e) {
        QMessageBox::warning(this, "Function", QString("Cannot evaluate f(x) = %1\n\n%2").arg(expr, e.what()));
        return false;
    }
    return true;
}

void MainWindow::refreshFunctionList(int selectRow) {
    if (!m_funcList) return;
    QSignalBlocker block(m_funcList);
    m_funcList->clear();
    for (int i : functionDatasetIndices()) {
        auto *item = new QListWidgetItem("f(x) = " + m_datasets[i].funcExpr);
        item->setForeground(QBrush(m_datasets[i].color));
        m_funcList->addItem(item);
    }
    if (selectRow >= 0 && selectRow < m_funcList->count()) m_funcList->setCurrentRow(selectRow);
    m_funcUpdateBtn->setEnabled(m_funcList->currentRow() >= 0 && !m_funcList->selectedItems().isEmpty());
    m_funcRemoveBtn->setEnabled(m_funcUpdateBtn->isEnabled());
}

// Rebuilds the X / Y selectors after the function datasets changed, keeping what was selected.
// removedDsIdx: a dataset that was just deleted (later indices move down by one).
void MainWindow::rebuildColumnsKeepSelection(int removedDsIdx) {
    auto remap = [removedDsIdx](CurveKey k) {
        if (removedDsIdx >= 0 && k.dsIdx > removedDsIdx) --k.dsIdx;
        return k;
    };
    QVector<CurveKey> selY, selY2;
    for (int row = 0; row < m_yList->count() && row < m_yItems.size(); ++row)
        if (m_yList->item(row)->isSelected() && m_yItems[row].dsIdx != removedDsIdx) selY << remap(m_yItems[row]);
    for (int row = 0; row < m_y2List->count() && row < m_y2Items.size(); ++row)
        if (m_y2List->item(row)->isSelected() && m_y2Items[row].dsIdx != removedDsIdx) selY2 << remap(m_y2Items[row]);

    rebuildXRows();
    rebuildYList();
    rebuildY2List();
    m_yList->clearSelection();
    m_y2List->clearSelection();
    for (const CurveKey &k : selY)
        for (int row = 0; row < m_yItems.size(); ++row)
            if (m_yItems[row].dsIdx == k.dsIdx && m_yItems[row].col == k.col) m_yList->item(row)->setSelected(true);
    for (const CurveKey &k : selY2)
        for (int row = 0; row < m_y2Items.size(); ++row)
            if (m_y2Items[row].dsIdx == k.dsIdx && m_y2Items[row].col == k.col) m_y2List->item(row)->setSelected(true);
    if (removedDsIdx >= 0) m_errColForCurve.clear();   // its keys refer to the old dataset numbers
    rebuildErrRows();
    refreshDatasetInfo();
}

void MainWindow::addFunction() {
    QString expr; double lo, hi; int n;
    if (!readFunctionInputs(expr, lo, hi, n)) return;
    Dataset ds = makeFunctionDataset(expr, lo, hi, n);
    ds.color = fileColors()[m_datasets.size() % fileColors().size()];
    m_datasets.push_back(ds);                       // functions are always the last datasets
    rebuildColumnsKeepSelection();
    refreshFunctionList(m_funcList->count());       // selects the new entry (list was rebuilt below)
    // select the new function's curve so it plots with the next "Plot"
    for (int row = 0; row < m_yItems.size(); ++row)
        if (m_yItems[row].dsIdx == m_datasets.size() - 1) m_yList->item(row)->setSelected(true);
    rebuildErrRows();
    onFunctionSelected();
}

void MainWindow::updateFunction() {
    int row = m_funcList->currentRow();
    QVector<int> fi = functionDatasetIndices();
    if (row < 0 || row >= fi.size() || m_funcList->selectedItems().isEmpty()) return;
    QString expr; double lo, hi; int n;
    if (!readFunctionInputs(expr, lo, hi, n)) return;
    int dsIdx = fi[row];
    Dataset ds = makeFunctionDataset(expr, lo, hi, n);
    ds.color = m_datasets[dsIdx].color;
    ds.xColumn = m_datasets[dsIdx].xColumn;
    m_datasets[dsIdx] = ds;
    rebuildColumnsKeepSelection();
    refreshFunctionList(row);
    clearPlot();                                    // what is drawn is out of date now
}

void MainWindow::removeFunction() {
    int row = m_funcList->currentRow();
    QVector<int> fi = functionDatasetIndices();
    if (row < 0 || row >= fi.size() || m_funcList->selectedItems().isEmpty()) return;
    int dsIdx = fi[row];
    m_datasets.removeAt(dsIdx);
    rebuildColumnsKeepSelection(dsIdx);
    refreshFunctionList(-1);
    clearPlot();
}

// Clicking a function in the list shows its expression, domain and samples in the fields.
void MainWindow::onFunctionSelected() {
    int row = m_funcList->currentRow();
    QVector<int> fi = functionDatasetIndices();
    bool has = row >= 0 && row < fi.size() && !m_funcList->selectedItems().isEmpty();
    m_funcUpdateBtn->setEnabled(has);
    m_funcRemoveBtn->setEnabled(has);
    if (!has) return;
    const Dataset &ds = m_datasets[fi[row]];
    m_funcExprEdit->setText(ds.funcExpr);
    m_funcMinEdit->setText(QString::number(ds.funcMin, 'g', 12));
    m_funcMaxEdit->setText(QString::number(ds.funcMax, 'g', 12));
    m_funcSamplesSpin->setValue(ds.funcSamples);
}

// ------------------------------------------------------------ plot options
QGroupBox *MainWindow::buildPlotOptionsBox() {
    auto *box = new QGroupBox("Plot options");
    auto *grid = new QGridLayout(box);

    m_styleLabel = new QLabel("Style:");
    grid->addWidget(m_styleLabel, 0, 0);
    m_styleCombo = new QComboBox;
    m_styleCombo->addItems({"Line", "Line + markers", "Markers only"});
    m_styleCombo->setMaximumWidth(160);
    grid->addWidget(m_styleCombo, 0, 1);

    m_logXCheck = new QCheckBox("Log X");
    m_logYCheck = new QCheckBox("Log Y");
    m_gridCheck = new QCheckBox("Grid");
    grid->addWidget(m_logXCheck, 1, 0);
    grid->addWidget(m_logYCheck, 1, 1);
    grid->addWidget(m_gridCheck, 2, 0);

    m_snapCheck = new QCheckBox("Snap draggable lines to nearest data point");
    grid->addWidget(m_snapCheck, 3, 0, 1, 2);

    m_errBarCheck = new QCheckBox(QString::fromUtf8("Error bars (\u00b1 standard deviation)"));
    grid->addWidget(m_errBarCheck, 4, 0, 1, 2);

    // ---- second Y axis ----
    m_y2Check = new QCheckBox("Second Y axis");
    grid->addWidget(m_y2Check, 6, 0, 1, 2);
    m_y2Frame = new QWidget;
    auto *y2Layout = new QVBoxLayout(m_y2Frame);
    y2Layout->setContentsMargins(16, 0, 0, 0);
    y2Layout->addWidget(new QLabel("Columns for the second (right) Y axis:"));
    m_y2List = new QListWidget;
    m_y2List->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_y2List->setFixedHeight(120);
    y2Layout->addWidget(m_y2List);
    m_logY2Check = new QCheckBox("Log Y2");
    y2Layout->addWidget(m_logY2Check);
    grid->addWidget(m_y2Frame, 7, 0, 1, 2);
    m_y2Frame->setVisible(false);
    connect(m_y2Check, &QCheckBox::toggled, this, [this](bool on) {
        m_y2Frame->setVisible(on);
        for (QWidget *w : m_y2limWidgets) w->setVisible(on);
        rebuildErrRows();
    });
    connect(m_y2List, &QListWidget::itemSelectionChanged, this, &MainWindow::rebuildErrRows);

    // ---- bar chart options ----
    m_barOptFrame = new QWidget;
    auto *barOuter = new QVBoxLayout(m_barOptFrame);
    barOuter->setContentsMargins(0, 0, 0, 0);
    auto *barLayout = new QHBoxLayout;
    barLayout->addWidget(new QLabel("Bar width:"));
    m_barWidthSpin = new QDoubleSpinBox;
    m_barWidthSpin->setRange(0.05, 1.0);
    m_barWidthSpin->setSingleStep(0.05);
    m_barWidthSpin->setValue(0.8);
    m_barWidthSpin->setToolTip("Width of a bar group as a fraction of the spacing between X values");
    barLayout->addWidget(m_barWidthSpin);
    barLayout->addWidget(new QLabel("Gap:"));
    m_barGapSpin = new QDoubleSpinBox;
    m_barGapSpin->setRange(0.0, 2.0);
    m_barGapSpin->setSingleStep(0.05);
    m_barGapSpin->setValue(0.0);
    m_barGapSpin->setToolTip("Space between the bars of one group (the bars around one tick), as a fraction of one bar's width.\n"
                             "0 = the bars touch.");
    barLayout->addWidget(m_barGapSpin);
    barLayout->addStretch();
    barOuter->addLayout(barLayout);
    m_flipCheck = new QCheckBox("Sideways bars (swap X and Y)");
    m_flipCheck->setToolTip("Draw horizontal bars: the categories / X values run up the vertical axis");
    barOuter->addWidget(m_flipCheck);
    m_numAsTextCheck = new QCheckBox("Treat X numbers as text (even bar spacing)");
    m_numAsTextCheck->setToolTip("Place the bars at equal distances, one per distinct X value, and label each with its number.\n"
                                 "Without it, bars sit at their numeric X position, so uneven X values give uneven gaps.");
    barOuter->addWidget(m_numAsTextCheck);
    grid->addWidget(m_barOptFrame, 8, 0, 1, 2);
    m_barOptFrame->setVisible(false);

    // ---- histogram options ----
    m_histOptFrame = new QWidget;
    auto *histOuter = new QVBoxLayout(m_histOptFrame);
    histOuter->setContentsMargins(0, 0, 0, 0);
    auto *histLayout = new QHBoxLayout;
    histLayout->addWidget(new QLabel("Bins:"));
    m_histBinsSpin = new QSpinBox;
    m_histBinsSpin->setRange(2, 500);
    m_histBinsSpin->setValue(10);
    histLayout->addWidget(m_histBinsSpin);
    histLayout->addWidget(new QLabel("Show:"));
    m_histNormCombo = new QComboBox;
    m_histNormCombo->addItems({"Count", "Probability", "Density"});
    m_histNormCombo->setToolTip("Count: values per bin\nProbability: share of all values (bars sum to 1)\n"
                                "Density: bars integrate to 1 (area = 1)");
    histLayout->addWidget(m_histNormCombo);
    histLayout->addStretch();
    histOuter->addLayout(histLayout);
    auto *histTickLayout = new QHBoxLayout;
    histTickLayout->addWidget(new QLabel("Bin ticks at:"));
    m_histTickCombo = new QComboBox;
    m_histTickCombo->addItems({"Auto", "Bin edges", "Bin centers"});
    m_histTickCombo->setToolTip("Where the ticks along the value axis (X, or Y when sideways) sit: automatic, on every bin edge, or in the middle of every bin");
    histTickLayout->addWidget(m_histTickCombo);
    histTickLayout->addStretch();
    histOuter->addLayout(histTickLayout);
    m_histFlipCheck = new QCheckBox("Sideways histogram (swap X and Y)");
    m_histFlipCheck->setToolTip("Draw horizontal bins: the values run up the vertical axis, the counts along the horizontal one");
    histOuter->addWidget(m_histFlipCheck);
    // the bar chart and histogram share one setting: keep both check boxes in step
    connect(m_flipCheck, &QCheckBox::toggled, m_histFlipCheck, &QCheckBox::setChecked);
    connect(m_histFlipCheck, &QCheckBox::toggled, m_flipCheck, &QCheckBox::setChecked);
    grid->addWidget(m_histOptFrame, 9, 0, 1, 2);

    // ---- color scheme of the curves / bars ----
    grid->addWidget(new QLabel("Color scheme:"), 10, 0);
    m_colorSchemeCombo = new QComboBox;
    for (const ColorScheme &sc : colorSchemes()) m_colorSchemeCombo->addItem(sc.name);
    m_colorSchemeCombo->setMaxVisibleItems(25);
    m_colorSchemeCombo->setToolTip("Colors used for the curves, bars and histograms (applied when you press Plot).\n"
                                   "The export window starts from the same scheme.");
    {
        int saved = colorSchemeIndex(QSettings().value("plot/colorScheme", defaultColorSchemeName()).toString());
        m_colorSchemeCombo->setCurrentIndex(saved >= 0 ? saved : colorSchemeIndex(defaultColorSchemeName()));
    }
    connect(m_colorSchemeCombo, &QComboBox::currentTextChanged, this,
            [](const QString &name) { QSettings().setValue("plot/colorScheme", name); });
    grid->addWidget(m_colorSchemeCombo, 10, 1);
    m_histOptFrame->setVisible(false);

    connect(m_plotTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { onPlotTypeChanged(); });

    // One std-dev selector per selected Y curve; only visible while error bars are on.
    m_errColFrame = new QWidget;
    m_errRowsLayout = new QVBoxLayout(m_errColFrame);
    m_errRowsLayout->setContentsMargins(16, 0, 0, 0);
    grid->addWidget(m_errColFrame, 5, 0, 1, 2);
    m_errColFrame->setVisible(false);
    connect(m_errBarCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_errColFrame->setVisible(on);
        if (on) rebuildErrRows();
    });
    connect(m_yList, &QListWidget::itemSelectionChanged, this, &MainWindow::rebuildErrRows);

    return box;
}

// -------------------------------------------------------------- file load
void MainWindow::openFiles() {
    QString startDir = m_lastOpenDir.isEmpty() ? QDir::homePath() : m_lastOpenDir;
    QStringList paths = QFileDialog::getOpenFileNames(
        this, "Select one or more data files", startDir,
        "Data files (*.csv *.txt *.tsv *.dat);;All files (*)");
    if (paths.isEmpty()) return;
    setLastOpenDir(QFileInfo(paths.first()).absolutePath());
    m_sepCombo->setCurrentIndex(0);
    m_decimalCombo->setCurrentIndex(0);
    loadFiles(paths);
}

void MainWindow::reparseFiles() {
    if (m_datasets.isEmpty()) {
        QMessageBox::information(this, "No file", "Open a file first.");
        return;
    }
    QStringList paths;
    QVector<Dataset> manual;
    for (const auto &ds : m_datasets) {
        if (ds.isFunction) continue;                      // functions are re-added by loadFiles
        if (ds.path == manualDataPath()) manual << ds;   // typed data has no file to re-read
        else paths << ds.path;
    }
    if (paths.isEmpty()) {
        QMessageBox::information(this, "Nothing to re-parse", "Only manually entered data is loaded; it has no file to re-parse.");
        return;
    }
    loadFiles(paths);
    for (const auto &ds : manual) addManualDataset(ds);  // loadFiles replaced everything; put it back
}

void MainWindow::openDataEntry() {
    if (!m_dataEntryDlg) {
        m_dataEntryDlg = new DataEntryDialog(this);
        connect(m_dataEntryDlg, &DataEntryDialog::datasetReady, this, &MainWindow::addManualDataset);
    }
    m_dataEntryDlg->show();
    m_dataEntryDlg->raise();
    m_dataEntryDlg->activateWindow();
}

// Adds (or replaces) the single "manual entry" dataset alongside any loaded files.
void MainWindow::addManualDataset(const Dataset &dsIn) {
    Dataset ds = dsIn;
    for (int i = m_datasets.size() - 1; i >= 0; --i)
        if (m_datasets[i].path == manualDataPath()) m_datasets.removeAt(i);
    ds.color = fileColors()[m_datasets.size() % fileColors().size()];
    int insertAt = m_datasets.size();                 // before the functions, which stay last
    for (int i = 0; i < m_datasets.size(); ++i)
        if (m_datasets[i].isFunction) { insertAt = i; break; }
    m_datasets.insert(insertAt, ds);
    refreshDatasetInfo();
    populateColumnWidgets();
    refreshFunctionList();
    clearPlot();
}

static QString sepOverrideFor(const QString &choice) {
    if (choice == "Comma  ( , )") return ",";
    if (choice == "Semicolon  ( ; )") return ";";
    if (choice == "Tab") return "\t";
    if (choice == "Pipe  ( | )") return "|";
    if (choice == "Whitespace") return "\\s+";
    return QString(); // Auto
}
static QString decimalOverrideFor(const QString &choice) {
    if (choice == "Dot  ( . )") return ".";
    if (choice == "Comma  ( , )") return ",";
    return QString(); // Auto
}

void MainWindow::loadFiles(const QStringList &paths) {
    QString sepOverride = sepOverrideFor(m_sepCombo->currentText());
    QString decOverride = decimalOverrideFor(m_decimalCombo->currentText());

    QVector<Dataset> newDatasets;
    QStringList failures;
    for (int i = 0; i < paths.size(); ++i) {
        try {
            Dataset ds = loadDataset(paths[i], sepOverride, decOverride);
            ds.color = fileColors()[i % fileColors().size()];
            newDatasets.push_back(ds);
        } catch (std::exception &e) {
            failures << QString("%1: %2").arg(QFileInfo(paths[i]).fileName(), e.what());
        }
    }

    if (!failures.isEmpty()) {
        QMessageBox::critical(this, failures.size() > 1 ? "Failed to load files" : "Failed to load file",
                               failures.join("\n") + "\n\nTry overriding the separator or decimal mark.");
    }
    if (newDatasets.isEmpty()) return;

    // plotted functions are not files: carry them over to the newly loaded set
    QVector<Dataset> functions;
    for (const auto &ds : m_datasets) if (ds.isFunction) functions << ds;
    m_datasets = newDatasets;
    for (Dataset fn : functions) {
        fn.color = fileColors()[m_datasets.size() % fileColors().size()];
        m_datasets.push_back(fn);
    }

    refreshDatasetInfo();

    populateColumnWidgets();
    refreshFunctionList();
    clearPlot();
}

void MainWindow::refreshDatasetInfo() {
    QStringList names;
    for (const auto &ds : m_datasets) if (!ds.isFunction) names << ds.basename;
    if (names.isEmpty()) {
        m_fileLabel->setText(m_datasets.isEmpty() ? "No file loaded" : "No file loaded (functions only)");
    } else if (names.size() == 1) {
        m_fileLabel->setText(names[0]);
    } else {
        m_fileLabel->setText(QString("%1 files loaded: %2").arg(names.size()).arg(names.join(", ")));
    }

    QStringList infoLines;
    for (const auto &ds : m_datasets) {
        const auto &info = ds.info;
        QString sepName = (info.sep == "\\s+") ? "whitespace" : QString("'%1'").arg(info.sep);
        QString skipNote = info.skippedRows > 0
                                ? QString("  (skipped %1 preamble lines)").arg(info.skippedRows)
                                : "";
        QString prefix = m_datasets.size() > 1 ? ds.basename + ": " : "";
        if (ds.isFunction) {
            infoLines << QString("%1Function  domain [%2, %3]  %4 samples")
                             .arg(prefix).arg(ds.funcMin, 0, 'g', 8).arg(ds.funcMax, 0, 'g', 8).arg(ds.funcSamples);
            continue;
        }
        if (ds.path == manualDataPath()) {
            infoLines << QString("%1Manual entry  Rows: %2  Cols: %3  Header: %4")
                             .arg(prefix).arg(info.rows).arg(info.cols).arg(info.hasHeader ? "yes" : "no");
            continue;
        }
        infoLines << QString("%1Rows: %2  Cols: %3  Sep: %4  Decimal: '%5'  Header: %6%7")
                         .arg(prefix).arg(info.rows).arg(info.cols).arg(sepName, info.decimal)
                         .arg(info.hasHeader ? "yes" : "no", skipNote);
    }
    m_infoLabel->setText(infoLines.join("\n"));
}

void MainWindow::populateColumnWidgets() {
    rebuildXRows();
    m_errColForCurve.clear();
    rebuildYList();
    rebuildY2List();
    rebuildErrRows();
}

// Rebuilds the per-curve std-dev selectors to match the currently selected Y columns.
// Choices are remembered per curve, so re-selecting a curve restores its std-dev column.
void MainWindow::rebuildErrRows() {
    if (!m_errRowsLayout) return;
    QLayoutItem *item;
    while ((item = m_errRowsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    if (!errorBarsEnabled()) return;

    QVector<CurveKey> selKeys;
    for (int row = 0; row < m_yList->count() && row < m_yItems.size(); ++row)
        if (m_yList->item(row)->isSelected()) selKeys << m_yItems[row];
    if (secondAxisEnabled())
        for (int row = 0; row < m_y2List->count() && row < m_y2Items.size(); ++row)
            if (m_y2List->item(row)->isSelected()) selKeys << m_y2Items[row];

    for (const CurveKey &key : selKeys) {
        if (key.dsIdx < 0 || key.dsIdx >= m_datasets.size()) continue;
        const Dataset &ds = m_datasets[key.dsIdx];

        auto *rowW = new QWidget;
        auto *hl = new QHBoxLayout(rowW);
        hl->setContentsMargins(0, 1, 0, 1);
        // Fixed-width, elided label so every combo box starts at the same x and has the
        // same width, however long the curve name is (full name is in the tooltip).
        constexpr int kLabelWidth = 120;
        QString fullLabel = curveLabel(key);
        auto *lbl = new QLabel;
        lbl->setFixedWidth(kLabelWidth);
        lbl->setText(QFontMetrics(lbl->font()).elidedText(fullLabel + ":", Qt::ElideMiddle, kLabelWidth));
        lbl->setToolTip(fullLabel);
        hl->addWidget(lbl);
        auto *combo = new QComboBox;
        // Size from a small fixed character count, not the longest column name, so long
        // names can't force the whole left panel wider.
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo->addItem("(none)");
        for (const auto &col : ds.columns) combo->addItem(col.name);
        int idx = combo->findText(m_errColForCurve.value(key));
        combo->setCurrentIndex(idx > 0 ? idx : 0);
        hl->addWidget(combo, 1);
        connect(combo, &QComboBox::currentTextChanged, this, [this, key](const QString &text) {
            if (text == "(none)") m_errColForCurve.remove(key);
            else m_errColForCurve[key] = text;
        });
        m_errRowsLayout->addWidget(rowW);
    }
}

QString MainWindow::errorColumnFor(const CurveKey &key) const {
    if (!errorBarsEnabled()) return QString();
    QString name = m_errColForCurve.value(key);
    if (name.isEmpty() || key.dsIdx < 0 || key.dsIdx >= m_datasets.size()) return QString();
    return m_datasets[key.dsIdx].columnIndex(name) >= 0 ? name : QString();
}

QVector<double> MainWindow::curveErrArray(const CurveKey &key) const {
    if (!errorBarsEnabled()) return {};
    QString name = errorColumnFor(key);
    if (name.isEmpty()) return {};
    const Dataset &ds = m_datasets[key.dsIdx];
    return ds.columns[ds.columnIndex(name)].values;
}

void MainWindow::removeErrorBars() {
    for (const auto &eb : m_errorBars)
        if (eb && m_plot->hasPlottable(eb.data())) m_plot->removePlottable(eb.data());
    m_errorBars.clear();
    // bar / histogram series (error bars above already detached from them)
    for (const auto &b : m_bars)
        if (b && m_plot->hasPlottable(b.data())) m_plot->removePlottable(b.data());
    m_bars.clear();
    if (m_barsGroup) { delete m_barsGroup.data(); m_barsGroup = nullptr; }
}

void MainWindow::rebuildXRows() {
    QLayoutItem *item;
    while ((item = m_xRowsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_xRows.clear();

    for (int dsIdx = 0; dsIdx < m_datasets.size(); ++dsIdx) {
        Dataset &ds = m_datasets[dsIdx];
        auto *row = new QWidget;
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 1, 0, 1);

        QPixmap swatch(12, 12);
        swatch.fill(ds.color);
        auto *swatchLabel = new QLabel;
        swatchLabel->setPixmap(swatch);
        hl->addWidget(swatchLabel);

        // Fixed-width, elided label so every X combo box lines up (full name in tooltip).
        constexpr int kXLabelWidth = 110;
        auto *nameLabel = new QLabel;
        nameLabel->setFixedWidth(kXLabelWidth);
        nameLabel->setText(QFontMetrics(nameLabel->font()).elidedText(ds.basename, Qt::ElideMiddle, kXLabelWidth));
        nameLabel->setToolTip(ds.basename);
        hl->addWidget(nameLabel);

        auto *combo = new QComboBox;
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo->addItem(indexLabel());
        for (const auto &col : ds.columns) combo->addItem(col.name);
        combo->setCurrentText(ds.xColumn);
        hl->addWidget(combo, 1);

        connect(combo, &QComboBox::currentTextChanged, this, [this, dsIdx](const QString &text) {
            if (dsIdx < m_datasets.size()) m_datasets[dsIdx].xColumn = text;
        });

        m_xRowsLayout->addWidget(row);
        m_xRows.push_back({dsIdx, combo});
    }
}

void MainWindow::rebuildYList() {
    m_yList->clear();
    m_yItems.clear();
    for (int dsIdx = 0; dsIdx < m_datasets.size(); ++dsIdx) {
        const Dataset &ds = m_datasets[dsIdx];
        QString prefix = m_datasets.size() > 1 ? ds.basename + ": " : "";
        for (const auto &col : ds.columns) {
            if (ds.isFunction && col.name != "f(x)") continue;   // x of a function is not a curve
            auto *item = new QListWidgetItem(ds.isFunction ? "f(x) = " + ds.funcExpr : prefix + col.name);
            item->setForeground(QBrush(ds.color));
            m_yList->addItem(item);
            m_yItems.push_back({dsIdx, col.name});
        }
    }
    if (m_yItems.size() > 1) m_yList->item(1)->setSelected(true);
    else if (!m_yItems.isEmpty()) m_yList->item(0)->setSelected(true);
}

void MainWindow::rebuildY2List() {
    m_y2List->clear();
    m_y2Items.clear();
    for (int dsIdx = 0; dsIdx < m_datasets.size(); ++dsIdx) {
        const Dataset &ds = m_datasets[dsIdx];
        QString prefix = m_datasets.size() > 1 ? ds.basename + ": " : "";
        for (const auto &col : ds.columns) {
            if (ds.isFunction && col.name != "f(x)") continue;
            auto *item = new QListWidgetItem(ds.isFunction ? "f(x) = " + ds.funcExpr : prefix + col.name);
            item->setForeground(QBrush(ds.color));
            m_y2List->addItem(item);
            CurveKey k{dsIdx, col.name};
            k.y2 = true;
            m_y2Items.push_back(k);
        }
    }
}

// ============================================================== plotting
QString MainWindow::curveLabelPlain(const CurveKey &key) const {
    if (key.dsIdx < 0 || key.dsIdx >= m_datasets.size()) return key.col;
    if (m_datasets[key.dsIdx].isFunction) return m_datasets[key.dsIdx].funcExpr;   // legend: the formula
    if (m_datasets.size() > 1) return m_datasets[key.dsIdx].basename + ": " + key.col;
    return key.col;
}

QString MainWindow::curveLabel(const CurveKey &key) const {
    return curveLabelPlain(key) + (key.y2 ? " [Y2]" : "");
}

QCPAxis *MainWindow::valueAxisFor(const CurveKey &key) const {
    if (m_flipBars) return key.y2 ? m_plot->xAxis2 : m_plot->xAxis;   // sideways bars: values run horizontally
    return key.y2 ? m_plot->yAxis2 : m_plot->yAxis;
}

// X as the bar chart sees it: with "numbers as text" every distinct numeric X value gets its own
// evenly spaced position 0, 1, 2, ... (text X columns already work like that).
QVector<double> MainWindow::curveXArray(const CurveKey &key) const {
    QVector<double> x = curveXArrayRaw(key);
    if (!m_numAsTextBars || m_plottedType != PlotType::Bar) return x;
    const Dataset *ds = (key.dsIdx >= 0 && key.dsIdx < m_datasets.size()) ? &m_datasets[key.dsIdx] : nullptr;
    if (ds) {
        int ci = ds->columnIndex(ds->xColumn);
        if (ci >= 0 && !ds->columns[ci].isNumeric) return x;   // a text column is already evenly spaced
    }
    for (double &v : x) {
        if (std::isnan(v)) continue;
        auto it = m_textPosMap.constFind(v);
        v = (it == m_textPosMap.constEnd()) ? std::numeric_limits<double>::quiet_NaN() : double(it.value());
    }
    return x;
}

QVector<double> MainWindow::curveXArrayRaw(const CurveKey &key) const {
    if (key.dsIdx < 0 || key.dsIdx >= m_datasets.size()) return {};
    const Dataset &ds = m_datasets[key.dsIdx];
    if (ds.xColumn == indexLabel()) {
        int n = ds.columns.isEmpty() ? 0 : ds.columns.first().values.size();
        QVector<double> idx(n);
        for (int i = 0; i < n; ++i) idx[i] = i;
        return idx;
    }
    int ci = ds.columnIndex(ds.xColumn);
    if (ci < 0) return {};
    if (!ds.columns[ci].isNumeric && !ds.columns[ci].labels.isEmpty()) {
        // text X column (categories): plot at positions 0, 1, 2, ...
        QVector<double> pos(ds.columns[ci].values.size());
        for (int i = 0; i < pos.size(); ++i) pos[i] = i;
        return pos;
    }
    return ds.columns[ci].values;
}

QStringList MainWindow::categoryLabels(const CurveKey &key) const {
    if (key.dsIdx < 0 || key.dsIdx >= m_datasets.size()) return {};
    const Dataset &ds = m_datasets[key.dsIdx];
    int ci = ds.xColumn == indexLabel() ? -1 : ds.columnIndex(ds.xColumn);
    if (ci >= 0 && !ds.columns[ci].isNumeric) return ds.columns[ci].labels;
    if (m_numAsTextBars && m_plottedType == PlotType::Bar) return m_textPosLabels;   // the numbers, as text
    return {};
}

QVector<double> MainWindow::curveYArray(const CurveKey &key) const {
    if (key.dsIdx < 0 || key.dsIdx >= m_datasets.size()) return {};
    const Dataset &ds = m_datasets[key.dsIdx];
    int ci = ds.columnIndex(key.col);
    if (ci < 0) return {};
    return ds.columns[ci].values;
}

void MainWindow::removeAllOverlays() {
    removeFitDraggables();
    removeIntegralDraggables();
    cancelInterpPlacement();
    removeInterpArtifacts();

    for (auto it = m_fitGraphs.begin(); it != m_fitGraphs.end(); ++it) m_plot->removePlottable(it.value());
    m_fitGraphs.clear();
    m_fitResults.clear();
    m_fitSlots.clear();
    m_activeFitSlot = -1;

    for (auto it = m_integralFills.begin(); it != m_integralFills.end(); ++it) m_plot->removePlottable(it.value());
    m_integralFills.clear();
    m_integralResults.clear();
    m_integralSlots.clear();
    m_activeIntegralSlot = -1;

    clearFitResultsText();
    clearIntegralResultsText();
    clearInterpResultsText();
}

void MainWindow::clearPlot() {
    removeAllOverlays();
    m_dataGraphs.clear();
    m_currentYCols.clear();
    removeErrorBars();
    m_fitColList->clear();
    m_integralList->clear();
    m_integralItems.clear();
    m_plot->clearGraphs();
    m_plot->clearItems();
    m_plot->legend->setVisible(false);
    auto *elem = qobject_cast<QCPTextElement *>(m_plot->plotLayout()->elementAt(0));
    if (elem) elem->setText(m_datasets.isEmpty() ? "No data plotted" : "Ready to plot");
    m_plot->xAxis->setLabel("");
    m_plot->yAxis->setLabel("");
    m_plot->yAxis2->setLabel("");
    m_plot->yAxis2->setVisible(false);
    m_plot->xAxis2->setLabel("");
    m_plot->xAxis2->setVisible(false);
    m_plot->replot();
    m_viewHistory.clear();
    m_viewHistoryPos = -1;
    updateNavButtonsEnabled();
}

void MainWindow::plot() {
    if (m_datasets.isEmpty()) {
        QMessageBox::information(this, "No data", "Open a file first.");
        return;
    }
    QVector<CurveKey> ySel, y2Sel;
    for (int row = 0; row < m_yList->count(); ++row)
        if (m_yList->item(row)->isSelected()) ySel.push_back(m_yItems[row]);
    if (secondAxisEnabled())
        for (int row = 0; row < m_y2List->count() && row < m_y2Items.size(); ++row)
            if (m_y2List->item(row)->isSelected()) y2Sel.push_back(m_y2Items[row]);
    if (ySel.isEmpty() && y2Sel.isEmpty()) {
        QMessageBox::information(this, "No Y column", "Select at least one Y column.");
        return;
    }
    const QVector<CurveKey> allSel = ySel + y2Sel;
    const QVector<CurveKey> &baseSel = ySel.isEmpty() ? y2Sel : ySel; // drives x label / title

    QString style = m_styleCombo->currentText();
    const PlotType type = plotType();
    m_plottedType = type;
    m_flipBars = (type == PlotType::Bar || type == PlotType::Histogram) && m_flipCheck->isChecked();
    m_plottedBarGap = m_barGapSpin->value();
    {
        // colors of this plot: the chosen scheme, cycled
        const ColorScheme &scheme = colorSchemes()[std::max(0, colorSchemeIndex(m_colorSchemeCombo->currentText()))];
        m_plottedScheme = scheme.name;
        m_plotColorCycle.clear();
        for (int i = 0; i < scheme.tokens.size(); ++i) m_plotColorCycle << schemeQtColor(scheme, i);
    }
    m_numAsTextBars = false;
    m_textPosMap.clear();
    m_textPosLabels.clear();
    if (type == PlotType::Bar && m_numAsTextCheck->isChecked()) {
        // one position per distinct numeric X value over every plotted series
        QSet<double> seen;
        QVector<double> distinct;
        for (const CurveKey &key : allSel) {
            const Dataset &ds = m_datasets[key.dsIdx];
            int ci = ds.xColumn == indexLabel() ? -1 : ds.columnIndex(ds.xColumn);
            if (ci >= 0 && !ds.columns[ci].isNumeric) continue;
            for (double v : curveXArrayRaw(key))
                if (!std::isnan(v) && !seen.contains(v)) { seen.insert(v); distinct << v; }
        }
        std::sort(distinct.begin(), distinct.end());
        for (int i = 0; i < distinct.size(); ++i) {
            m_textPosMap.insert(distinct[i], i);
            m_textPosLabels << QString::number(distinct[i], 'g', 10);
        }
        m_numAsTextBars = !distinct.isEmpty();
    }

    removeAllOverlays();
    removeErrorBars();
    m_plot->clearGraphs();
    m_plot->clearItems();
    m_dataGraphs.clear();
    m_histData.clear();
    m_seriesColors.clear();

    bool plottedAny = false;
    int colorIdx = 0;
    // Antialiasing and per-point scatter markers are what actually make
    // QCustomPlot slow to draw/replot on very large curves (adaptive
    // sampling already thins the *line* automatically, but not markers).
    // Past this many points, disable AA for that curve and only draw a
    // marker every so often -- the line itself is unaffected and still
    // passes through every data point.
    constexpr int kLargeCurveThreshold = 5000;
    constexpr int kMaxVisibleMarkers = 2000;
    if (type != PlotType::Line) plottedAny = buildBarSeries(allSel, type);
    for (const CurveKey &key : allSel) {
        if (type != PlotType::Line) break;   // bars / histograms were built above
        QVector<double> xFull = curveXArray(key);
        QVector<double> yFull = curveYArray(key);
        QVector<double> errFull = curveErrArray(key);
        bool useErr = !errFull.isEmpty();
        QVector<double> xs, ys, errs;
        int n = std::min(xFull.size(), yFull.size());
        xs.reserve(n);
        ys.reserve(n);
        for (int i = 0; i < n; ++i) {
            if (!std::isnan(xFull[i]) && !std::isnan(yFull[i])) {
                xs << xFull[i]; ys << yFull[i];
                if (useErr) {
                    double e = (i < errFull.size()) ? errFull[i] : std::numeric_limits<double>::quiet_NaN();
                    errs << (std::isnan(e) ? 0.0 : std::fabs(e)); // missing std-dev -> no bar
                }
            }
        }
        if (xs.isEmpty()) continue;
        bool isLarge = xs.size() > kLargeCurveThreshold;

        QCPGraph *g = m_plot->addGraph(m_plot->xAxis, valueAxisFor(key));
        g->setData(xs, ys, true);
        g->setSelectable(QCP::stNone);
        g->setAntialiased(!isLarge);
        QColor c = m_plotColorCycle[colorIdx % m_plotColorCycle.size()];
        colorIdx++;
        QPen pen(c);
        pen.setWidthF(1.6);
        g->setPen(pen);
        if (style == "Line") {
            g->setLineStyle(QCPGraph::lsLine);
            g->setScatterStyle(QCPScatterStyle::ssNone);
        } else if (style == "Line + markers") {
            g->setLineStyle(QCPGraph::lsLine);
            g->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, c, c, 5));
        } else {
            g->setLineStyle(QCPGraph::lsNone);
            g->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, c, c, 4));
        }
        if (isLarge && g->scatterStyle().shape() != QCPScatterStyle::ssNone)
            g->setScatterSkip(xs.size() / kMaxVisibleMarkers);
        g->setName(curveLabel(key));
        m_dataGraphs[key] = g;
        if (useErr) {
            // errs is aligned 1:1 with the graph's data (same filter, same order)
            auto *eb = new QCPErrorBars(m_plot->xAxis, valueAxisFor(key));
            eb->setDataPlottable(g);
            eb->setData(errs);
            eb->setErrorType(QCPErrorBars::etValueError);
            QPen ebPen(c);
            ebPen.setWidthF(1.2);
            eb->setPen(ebPen);
            eb->setWhiskerWidth(6);
            eb->setSelectable(QCP::stNone);
            eb->setAntialiased(!isLarge);
            m_errorBars << eb;
        }
        plottedAny = true;
    }

    if (!plottedAny) {
        auto *elem = qobject_cast<QCPTextElement *>(m_plot->plotLayout()->elementAt(0));
        if (elem) elem->setText("Nothing plotted");
        m_plot->replot();
        return;
    }

    m_currentYCols = allSel;
    populateFitAndIntegralLists();

    int firstDsIdx = baseSel.first().dsIdx;
    for (const auto &k : baseSel) firstDsIdx = std::min(firstDsIdx, k.dsIdx);
    const Dataset &firstDs = m_datasets[firstDsIdx];
    QString xv = firstDs.xColumn;
    QString xlabel = (xv == indexLabel()) ? "Row index" : xv;
    if (type == PlotType::Histogram) xlabel = (baseSel.size() == 1) ? baseSel.first().col : QString("Value");
    m_plot->xAxis->setLabel(xlabel);

    QVector<CurveKey> firstFileCurves;
    for (const auto &k : baseSel) if (k.dsIdx == firstDsIdx) firstFileCurves << k;
    m_plot->yAxis->setLabel(ySel.isEmpty() ? QString()
                                           : (firstFileCurves.size() == 1 ? firstFileCurves.first().col : "Value"));

    // second Y axis: visible only when curves are assigned to it
    bool useY2 = !y2Sel.isEmpty();
    m_plot->yAxis2->setVisible(useY2);
    m_plot->yAxis2->setTickLabels(true);
    m_plot->yAxis2->grid()->setVisible(false);
    if (useY2) {
        QVector<CurveKey> y2First;
        for (const auto &k : y2Sel) if (k.dsIdx == y2Sel.first().dsIdx) y2First << k;
        m_plot->yAxis2->setLabel(y2First.size() == 1 ? y2First.first().col : "Value 2");
    } else {
        m_plot->yAxis2->setLabel("");
    }
    for (QWidget *w : m_y2limWidgets) w->setVisible(secondAxisEnabled());
    if (type == PlotType::Histogram) {
        m_plot->yAxis->setLabel(ySel.isEmpty() ? QString() : histNormalizationName());
        if (useY2) m_plot->yAxis2->setLabel(histNormalizationName());
    }
    if (m_flipBars) {
        // sideways bars: the values run along the bottom (and top) axis, the categories up the left one
        QString cat = m_plot->xAxis->label(), val = m_plot->yAxis->label(), val2 = m_plot->yAxis2->label();
        m_plot->xAxis->setLabel(val);
        m_plot->yAxis->setLabel(cat);
        m_plot->xAxis2->setLabel(val2);
        m_plot->yAxis2->setLabel("");
        m_plot->yAxis2->setVisible(false);
        m_plot->xAxis2->setVisible(useY2);
        m_plot->xAxis2->setTickLabels(true);
        m_plot->xAxis2->grid()->setVisible(false);
    } else {
        m_plot->xAxis2->setVisible(false);
        m_plot->xAxis2->setLabel("");
    }

    QStringList labels;
    for (const auto &k : firstFileCurves) labels << curveLabel(k);
    for (const auto &k : y2Sel) if (k.dsIdx == firstDsIdx) labels << curveLabel(k);
    auto *elem = qobject_cast<QCPTextElement *>(m_plot->plotLayout()->elementAt(0));
    if (elem) elem->setText(type == PlotType::Histogram ? labels.join(", ") + " (histogram)"
                                                         : labels.join(", ") + " vs " + xlabel);

    // Log scales only make sense for line plots (bars start at zero; categories aren't numeric).
    if (type == PlotType::Line && m_logXCheck->isChecked()) {
        m_plot->xAxis->setScaleType(QCPAxis::stLogarithmic);
        m_plot->xAxis->setTicker(QSharedPointer<QCPAxisTickerLog>(new QCPAxisTickerLog));
    } else {
        m_plot->xAxis->setScaleType(QCPAxis::stLinear);
        if (type == PlotType::Line) m_plot->xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
        // bar charts keep the (possibly text) ticker that buildBarSeries() installed
    }
    if (type == PlotType::Line && m_logYCheck->isChecked()) {
        m_plot->yAxis->setScaleType(QCPAxis::stLogarithmic);
        m_plot->yAxis->setTicker(QSharedPointer<QCPAxisTickerLog>(new QCPAxisTickerLog));
    } else {
        m_plot->yAxis->setScaleType(QCPAxis::stLinear);
        if (type == PlotType::Line) m_plot->yAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
    }
    if (type == PlotType::Line && m_logY2Check->isChecked()) {
        m_plot->yAxis2->setScaleType(QCPAxis::stLogarithmic);
        m_plot->yAxis2->setTicker(QSharedPointer<QCPAxisTickerLog>(new QCPAxisTickerLog));
    } else {
        m_plot->yAxis2->setScaleType(QCPAxis::stLinear);
        if (type == PlotType::Line) m_plot->yAxis2->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
    }
    m_plot->xAxis->grid()->setVisible(m_gridCheck->isChecked());
    m_plot->yAxis->grid()->setVisible(m_gridCheck->isChecked());
    m_plot->legend->setVisible(false);

    m_plot->rescaleAxes();
    if (type != PlotType::Line) {
        // a little room before the first / after the last bar (along the category axis) ...
        QCPAxis *catAxis = m_flipBars ? m_plot->yAxis : m_plot->xAxis;
        QCPRange xr = catAxis->range();
        double xpad = 0.03 * xr.size();
        catAxis->setRange(xr.lower - xpad, xr.upper + xpad);
        // ... and headroom above the tallest bar / error bar (along the value axes)
        const QList<QCPAxis *> valueAxes = m_flipBars ? QList<QCPAxis *>{m_plot->xAxis, m_plot->xAxis2}
                                                      : QList<QCPAxis *>{m_plot->yAxis, m_plot->yAxis2};
        for (QCPAxis *ax : valueAxes) {
            QCPRange r = ax->range();
            if (r.upper > 0) ax->setRange(r.lower, r.upper + 0.06 * r.size());
        }
    }
    m_plot->replot();

    syncAxisLimitEdits();
    resetViewHistory();

    if (type == PlotType::Line) {
        if (m_fitEnableCheck->isChecked()) startFitPlacement();
        if (m_integralEnableCheck->isChecked()) startIntegralPlacement();
        if (m_interpEnableCheck->isChecked()) startInterpPlacement();
    }
}

// ------------------------------------------------------- bar / histogram plots
MainWindow::PlotType MainWindow::plotType() const {
    switch (m_plotTypeCombo ? m_plotTypeCombo->currentIndex() : 0) {
    case 1: return PlotType::Bar;
    case 2: return PlotType::Histogram;
    default: return PlotType::Line;
    }
}

QString MainWindow::histNormalizationName() const {
    return m_histNormCombo ? m_histNormCombo->currentText() : QString("Count");
}

bool MainWindow::histogramPublic(const CurveKey &key, QVector<double> &edges, QVector<double> &heights) const {
    auto it = m_histData.constFind(key);
    if (it == m_histData.constEnd()) return false;
    edges = it.value().first;
    heights = it.value().second;
    return true;
}

// Shows only the controls that apply to the chosen plot type.
void MainWindow::onPlotTypeChanged() {
    PlotType t = plotType();
    bool line = (t == PlotType::Line), bar = (t == PlotType::Bar), hist = (t == PlotType::Histogram);
    m_styleLabel->setVisible(line);
    m_styleCombo->setVisible(line);
    m_barOptFrame->setVisible(bar);
    m_histOptFrame->setVisible(hist);

    // fit, integral and cursor/interpolation only make sense for curves
    if (!line) {
        m_fitEnableCheck->setChecked(false);
        m_integralEnableCheck->setChecked(false);
        m_interpEnableCheck->setChecked(false);
    }
    m_funcBox->setVisible(line);
    m_fitBox->setVisible(line);
    m_integralBox->setVisible(line);
    m_interpBox->setVisible(line);
    m_snapCheck->setVisible(line);       // snapping belongs to the draggable fit / integral lines
    m_logXCheck->setVisible(line);
    m_logYCheck->setVisible(line);
    m_logY2Check->setVisible(line);
    // X is irrelevant for a histogram; error bars don't apply to bins
    m_xRowsLabel->setVisible(!hist);
    m_xRowsContainer->setVisible(!hist);
    m_errBarCheck->setVisible(!hist);
    if (hist) m_errBarCheck->setChecked(false);
}

// Builds the bar (grouped) or histogram (overlaid) series. Returns false if nothing to draw.
bool MainWindow::buildBarSeries(const QVector<CurveKey> &allSel, PlotType type) {
    // start from plain numeric tickers; the category axis may get a text ticker below
    for (QCPAxis *ax : {m_plot->xAxis, m_plot->yAxis, m_plot->xAxis2, m_plot->yAxis2})
        ax->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker));
    QCPAxis *keyAxis = m_flipBars ? m_plot->yAxis : m_plot->xAxis;   // bars are placed along this axis

    // ------------------------------------------------------------ histogram
    if (type == PlotType::Histogram) {
        struct S { CurveKey key; QVector<double> vals; };
        QVector<S> series;
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const CurveKey &key : allSel) {
            S s; s.key = key;
            for (double v : curveYArray(key)) {
                if (std::isnan(v)) continue;
                s.vals << v;
                lo = std::min(lo, v); hi = std::max(hi, v);
            }
            if (!s.vals.isEmpty()) series << s;
        }
        if (series.isEmpty()) return false;
        if (!(hi > lo)) { lo -= 0.5; hi += 0.5; }
        const int bins = m_histBinsSpin->value();
        const double bw = (hi - lo) / bins;
        const int norm = m_histNormCombo->currentIndex();   // 0 count, 1 probability, 2 density

        int colorIdx = 0;
        QVector<double> lastEdges;
        for (const S &s : series) {
            QVector<double> counts(bins, 0.0);
            for (double v : s.vals) {
                int b = int((v - lo) / bw);
                counts[std::min(bins - 1, std::max(0, b))] += 1.0;   // the maximum falls in the last bin
            }
            const double n = s.vals.size();
            QVector<double> heights(bins), centers(bins), edges(bins + 1);
            for (int i = 0; i < bins; ++i) {
                heights[i] = norm == 0 ? counts[i] : norm == 1 ? counts[i] / n : counts[i] / (n * bw);
                centers[i] = lo + (i + 0.5) * bw;
                edges[i] = lo + i * bw;
            }
            edges[bins] = hi;

            auto *bars = new QCPBars(keyAxis, valueAxisFor(s.key));   // bins run along the key axis (y when sideways)
            bars->setData(centers, heights, true);
            bars->setWidthType(QCPBars::wtPlotCoords);
            bars->setWidth(bw);
            QColor c = m_plotColorCycle[colorIdx % m_plotColorCycle.size()];
            ++colorIdx;
            m_seriesColors[s.key] = c;
            QColor fill = c;
            fill.setAlpha(series.size() > 1 ? 140 : 210);
            bars->setBrush(fill);
            QPen pen(c.darker(150));
            pen.setWidthF(1.0);
            bars->setPen(pen);
            bars->setName(curveLabel(s.key));
            bars->setSelectable(QCP::stNone);
            m_bars << bars;
            m_histData[s.key] = qMakePair(edges, heights);
            lastEdges = edges;
        }
        // optional ticks on every bin edge / bin centre (skipped when there are too many to read)
        const int tickMode = m_histTickCombo->currentIndex();   // 0 auto, 1 edges, 2 centres
        if (tickMode != 0 && bins <= 30) {
            QSharedPointer<QCPAxisTickerText> text(new QCPAxisTickerText);
            if (tickMode == 1) for (double e : lastEdges) text->addTick(e, QString::number(e, 'g', 4));
            else for (int i = 0; i < bins; ++i) {
                double c = 0.5 * (lastEdges[i] + lastEdges[i + 1]);
                text->addTick(c, QString::number(c, 'g', 4));
            }
            keyAxis->setTicker(text);
        }
        return true;
    }

    // ------------------------------------------------------------------ bars
    struct B { CurveKey key; QVector<double> x, y, err; };
    QVector<B> series;
    QStringList categories;      // label for each integer position (text X column)
    for (const CurveKey &key : allSel) {
        QVector<double> xFull = curveXArray(key), yFull = curveYArray(key), eFull = curveErrArray(key);
        QStringList cats = categoryLabels(key);
        if (categories.isEmpty() && !cats.isEmpty()) categories = cats;
        const bool useErr = !eFull.isEmpty();
        int n = std::min(xFull.size(), yFull.size());
        QVector<int> idx;
        for (int i = 0; i < n; ++i)
            if (!std::isnan(xFull[i]) && !std::isnan(yFull[i])) idx << i;
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return xFull[a] < xFull[b]; });
        B b; b.key = key;
        for (int i : idx) {
            b.x << xFull[i]; b.y << yFull[i];
            if (useErr) {
                double e = i < eFull.size() ? eFull[i] : std::numeric_limits<double>::quiet_NaN();
                b.err << (std::isnan(e) ? 0.0 : std::fabs(e));
            }
        }
        if (!b.x.isEmpty()) series << b;
    }
    if (series.isEmpty()) return false;

    // spacing between neighbouring X values decides how wide the bars can be
    QVector<double> allX;
    for (const B &b : series) allX << b.x;
    std::sort(allX.begin(), allX.end());
    double spacing = 1.0, minDiff = std::numeric_limits<double>::infinity();
    for (int i = 1; i < allX.size(); ++i) {
        double d = allX[i] - allX[i - 1];
        if (d > 1e-12 * std::max(1.0, std::fabs(allX[i]))) minDiff = std::min(minDiff, d);
    }
    if (std::isfinite(minDiff)) spacing = minDiff;
    const int K = series.size();
    // a group of K bars with K-1 gaps (each a fraction of one bar's width) fills the chosen share of the spacing
    const double gapFrac = m_barGapSpin->value();
    const double barW = spacing * m_barWidthSpin->value() / (K + (K - 1) * gapFrac);

    if (K > 1) {
        m_barsGroup = new QCPBarsGroup(m_plot);
        m_barsGroup->setSpacingType(QCPBarsGroup::stPlotCoords);
        m_barsGroup->setSpacing(gapFrac * barW);
    }
    int colorIdx = 0;
    for (const B &b : series) {
        auto *bars = new QCPBars(keyAxis, valueAxisFor(b.key));
        bars->setData(b.x, b.y, true);
        bars->setWidthType(QCPBars::wtPlotCoords);
        bars->setWidth(barW);
        QColor c = m_plotColorCycle[colorIdx % m_plotColorCycle.size()];
        ++colorIdx;
        m_seriesColors[b.key] = c;
        QColor fill = c;
        fill.setAlpha(215);
        bars->setBrush(fill);
        QPen pen(c.darker(150));
        pen.setWidthF(1.0);
        bars->setPen(pen);
        bars->setName(curveLabel(b.key));
        bars->setSelectable(QCP::stNone);
        if (m_barsGroup) bars->setBarsGroup(m_barsGroup);
        m_bars << bars;

        if (!b.err.isEmpty()) {
            auto *eb = new QCPErrorBars(bars->keyAxis(), bars->valueAxis());
            eb->setDataPlottable(bars);
            eb->setData(b.err);
            eb->setErrorType(QCPErrorBars::etValueError);
            QPen ebPen(c.darker(200));
            ebPen.setWidthF(1.2);
            eb->setPen(ebPen);
            eb->setWhiskerWidth(6);
            eb->setSelectable(QCP::stNone);
            m_errorBars << eb;
        }
    }

    // text X column: tick labels at the category positions
    if (!categories.isEmpty()) {
        QSharedPointer<QCPAxisTickerText> text(new QCPAxisTickerText);
        QSet<int> done;
        for (const B &b : series)
            for (double x : b.x) {
                int p = int(std::lround(x));
                if (p >= 0 && p < categories.size() && !done.contains(p)) { text->addTick(p, categories[p]); done.insert(p); }
            }
        keyAxis->setTicker(text);
    }
    return true;
}

void MainWindow::autoscaleAxes() {
    m_plot->rescaleAxes();
    m_plot->replot();
    syncAxisLimitEdits();
}

void MainWindow::applyAxisLimits() {
    bool ok1, ok2, ok3, ok4;
    double xmin = m_xlimMinEdit->text().toDouble(&ok1);
    double xmax = m_xlimMaxEdit->text().toDouble(&ok2);
    double ymin = m_ylimMinEdit->text().toDouble(&ok3);
    double ymax = m_ylimMaxEdit->text().toDouble(&ok4);
    if (ok1 && ok2 && xmin < xmax) m_plot->xAxis->setRange(xmin, xmax);
    if (ok3 && ok4 && ymin < ymax) m_plot->yAxis->setRange(ymin, ymax);
    if (secondValueAxis()->visible() && m_y2limMinEdit) {
        bool ok5, ok6;
        double y2min = m_y2limMinEdit->text().toDouble(&ok5);
        double y2max = m_y2limMaxEdit->text().toDouble(&ok6);
        if (ok5 && ok6 && y2min < y2max) secondValueAxis()->setRange(y2min, y2max);
    }
    m_plot->replot();
}

// ================================================================== fit UI
QGroupBox *MainWindow::buildFitBox() {
    auto *box = new QGroupBox("Curve fit");
    auto *layout = new QVBoxLayout(box);

    m_fitEnableCheck = new QCheckBox("Enable curve fit (click plot to place range)");
    connect(m_fitEnableCheck, &QCheckBox::toggled, this, &MainWindow::onFitEnableToggled);
    layout->addWidget(m_fitEnableCheck);

    auto *typeRow = new QHBoxLayout;
    typeRow->addWidget(new QLabel("Type:"));
    m_fitTypeCombo = new QComboBox;
    m_fitTypeCombo->addItems(fitTypeNames());
    connect(m_fitTypeCombo, &QComboBox::currentTextChanged, this, &MainWindow::onFitTypeChanged);
    typeRow->addWidget(m_fitTypeCombo, 1);
    layout->addLayout(typeRow);

    m_polyFrame = new QWidget;
    auto *polyLayout = new QHBoxLayout(m_polyFrame);
    polyLayout->setContentsMargins(0, 0, 0, 0);
    polyLayout->addWidget(new QLabel("Degree:"));
    m_polyDegreeSpin = new QSpinBox;
    m_polyDegreeSpin->setRange(1, 12);
    m_polyDegreeSpin->setValue(2);
    connect(m_polyDegreeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::computeAndDrawFit);
    polyLayout->addWidget(m_polyDegreeSpin);
    polyLayout->addStretch();
    layout->addWidget(m_polyFrame);
    m_polyFrame->setVisible(false);

    m_customFrame = new QWidget;
    auto *customLayout = new QGridLayout(m_customFrame);
    customLayout->setContentsMargins(0, 0, 0, 0);
    customLayout->addWidget(new QLabel("f(x) ="), 0, 0);
    m_customExprEdit = new QLineEdit("a*exp(b*x)+c");
    customLayout->addWidget(m_customExprEdit, 0, 1);
    customLayout->addWidget(new QLabel("Params:"), 1, 0);
    m_customParamsEdit = new QLineEdit("a,b,c");
    customLayout->addWidget(m_customParamsEdit, 1, 1);
    customLayout->addWidget(new QLabel("Initial guess:"), 2, 0);
    m_customGuessEdit = new QLineEdit("1,1,1");
    customLayout->addWidget(m_customGuessEdit, 2, 1);
    connect(m_customExprEdit, &QLineEdit::editingFinished, this, &MainWindow::computeAndDrawFit);
    connect(m_customParamsEdit, &QLineEdit::editingFinished, this, &MainWindow::computeAndDrawFit);
    connect(m_customGuessEdit, &QLineEdit::editingFinished, this, &MainWindow::computeAndDrawFit);
    layout->addWidget(m_customFrame);
    m_customFrame->setVisible(false);

    auto *rangeRow = new QHBoxLayout;
    rangeRow->addWidget(new QLabel("Range:"));
    m_fitXminEdit = new QLineEdit;
    m_fitXminEdit->setPlaceholderText("xmin");
    m_fitXmaxEdit = new QLineEdit;
    m_fitXmaxEdit->setPlaceholderText("xmax");
    rangeRow->addWidget(m_fitXminEdit);
    rangeRow->addWidget(m_fitXmaxEdit);
    auto *applyRangeBtn = new QPushButton("Apply");
    connect(applyRangeBtn, &QPushButton::clicked, this, &MainWindow::onApplyFitRangeEntry);
    rangeRow->addWidget(applyRangeBtn);
    layout->addLayout(rangeRow);

    auto *navRow = new QHBoxLayout;
    auto *prevBtn = new QPushButton(QString::fromUtf8("\u25c0"));
    prevBtn->setMaximumWidth(32);
    connect(prevBtn, &QPushButton::clicked, this, &MainWindow::onPrevFitInterval);
    navRow->addWidget(prevBtn);
    m_fitNavLabel = new QLabel;
    m_fitNavLabel->setAlignment(Qt::AlignCenter);
    navRow->addWidget(m_fitNavLabel, 1);
    auto *nextBtn = new QPushButton(QString::fromUtf8("\u25b6"));
    nextBtn->setMaximumWidth(32);
    connect(nextBtn, &QPushButton::clicked, this, &MainWindow::onNextFitInterval);
    navRow->addWidget(nextBtn);
    layout->addLayout(navRow);

    auto *newIntervalBtn = new QPushButton("+ New interval");
    connect(newIntervalBtn, &QPushButton::clicked, this, &MainWindow::onNewFitInterval);
    layout->addWidget(newIntervalBtn);

    layout->addWidget(new QLabel("Columns to fit:"));
    m_fitColList = new QListWidget;
    m_fitColList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fitColList->setFixedHeight(140); // same height as the integral list
    connect(m_fitColList, &QListWidget::itemSelectionChanged, this, &MainWindow::onFitColumnSelectionChanged);
    layout->addWidget(m_fitColList);

    m_fitResultsText = new QTextEdit;
    m_fitResultsText->setReadOnly(true);
    m_fitResultsText->setMaximumHeight(120);
    layout->addWidget(m_fitResultsText);

    return box;
}

void MainWindow::onFitTypeChanged() {
    QString t = m_fitTypeCombo->currentText();
    m_polyFrame->setVisible(t == "Polynomial");
    m_customFrame->setVisible(t == "Custom");
    computeAndDrawFit();
}

void MainWindow::onFitEnableToggled(bool on) {
    if (on) {
        if (m_datasets.isEmpty() || m_currentYCols.isEmpty()) {
            QMessageBox::information(this, "No plot", "Plot some data first, then enable the fit.");
            m_fitEnableCheck->blockSignals(true);
            m_fitEnableCheck->setChecked(false);
            m_fitEnableCheck->blockSignals(false);
            return;
        }
        startFitPlacement();
    } else {
        cancelFitPlacement();
        removeFitDraggables();
        for (auto it = m_fitGraphs.begin(); it != m_fitGraphs.end(); ++it) m_plot->removePlottable(it.value());
        m_fitGraphs.clear();
        m_fitResults.clear();
        m_fitSlots.clear();
        m_activeFitSlot = -1;
        clearFitResultsText();
        updateFitNavLabel();
        rebuildIntegralListWidget();
        m_plot->replot();
    }
}

void MainWindow::startFitPlacement() {
    removeFitDraggables();
    for (auto it = m_fitGraphs.begin(); it != m_fitGraphs.end(); ++it) m_plot->removePlottable(it.value());
    m_fitGraphs.clear();
    m_fitResults.clear();
    m_fitSlots.clear();
    m_activeFitSlot = -1;
    cancelFitPlacement();
    m_fitPendingXs.clear();
    m_fitPlacementActive = true;
    m_fitResultsText->setPlainText("Click on the plot to place the first fit-range line.");
    updateFitNavLabel();
}

void MainWindow::cancelFitPlacement() {
    m_fitPlacementActive = false;
    m_fitPendingXs.clear();
}

void MainWindow::onPlotClickedForFit(QMouseEvent *event) {
    double x = m_plot->xAxis->pixelToCoord(event->pos().x());
    double y = m_plot->yAxis->pixelToCoord(event->pos().y());
    if (!m_plot->axisRect()->rect().contains(event->pos())) return;
    Q_UNUSED(y);
    m_fitPendingXs << x;
    if (m_fitPendingXs.size() < 2) {
        m_fitResultsText->setPlainText("Click on the plot to place the second fit-range line.");
        return;
    }
    double x0 = m_fitPendingXs[0], x1 = m_fitPendingXs[1];
    if (x0 > x1) std::swap(x0, x1);
    cancelFitPlacement();
    beginNewFitInterval(x0, x1);
}

void MainWindow::onNewFitInterval() {
    if (m_datasets.isEmpty() || m_currentYCols.isEmpty()) {
        QMessageBox::information(this, "No plot", "Plot some data first.");
        return;
    }
    if (!m_fitEnableCheck->isChecked()) {
        m_fitEnableCheck->blockSignals(true);
        m_fitEnableCheck->setChecked(true);
        m_fitEnableCheck->blockSignals(false);
    }
    removeFitDraggables();
    cancelFitPlacement();
    m_fitPendingXs.clear();
    m_fitPlacementActive = true;
    m_fitResultsText->setPlainText("Click on the plot to place the first fit-range line for the new interval.");
}

void MainWindow::beginNewFitInterval(double x0, double x1) {
    FitSlot slot;
    slot.xmin = x0; slot.xmax = x1;
    m_fitSlots.push_back(slot);
    m_activeFitSlot = m_fitSlots.size() - 1;
    showFitInterval(m_activeFitSlot);
}

void MainWindow::showFitInterval(int idx) {
    if (idx < 0 || idx >= m_fitSlots.size()) return;
    m_activeFitSlot = idx;
    removeFitDraggables();
    const FitSlot &slot = m_fitSlots[idx];
    m_fitXminEdit->setText(QString::number(slot.xmin, 'g', 6));
    m_fitXmaxEdit->setText(QString::number(slot.xmax, 'g', 6));

    m_fitVLines = new DraggableVLineGroup(
        m_plot, { slot.xmin, slot.xmax }, { QColor("firebrick"), QColor("firebrick") },
        [this]() { computeAndDrawFit(); },
        [this]() { return m_snapCheck->isChecked() ? snapValuesForCurrentPlot() : QVector<double>(); });

    // restore this interval's column selection (falls back to "all
    // selected" the first time a slot is shown, since slot.cols starts empty)
    m_fitColList->blockSignals(true);
    for (int row = 0; row < m_fitColList->count() && row < m_currentYCols.size(); ++row) {
        bool sel = slot.cols.isEmpty() || slot.cols.contains(m_currentYCols[row]);
        m_fitColList->item(row)->setSelected(sel);
    }
    m_fitColList->blockSignals(false);

    updateFitNavLabel();
    computeAndDrawFit();
}

void MainWindow::removeFitDraggables() {
    if (m_fitVLines) { m_fitVLines->remove(); delete m_fitVLines; m_fitVLines = nullptr; }
}

void MainWindow::onPrevFitInterval() {
    if (m_fitSlots.isEmpty()) return;
    int idx = (m_activeFitSlot <= 0) ? m_fitSlots.size() - 1 : m_activeFitSlot - 1;
    showFitInterval(idx);
}

void MainWindow::onNextFitInterval() {
    if (m_fitSlots.isEmpty()) return;
    int idx = (m_activeFitSlot + 1) % m_fitSlots.size();
    showFitInterval(idx);
}

void MainWindow::updateFitNavLabel() {
    int n = m_fitSlots.size();
    if (n == 0) { m_fitNavLabel->setText(""); return; }
    if (m_activeFitSlot < 0) { m_fitNavLabel->setText(QString("Interval %1/%1 (placing...)").arg(n + 1)); return; }
    m_fitNavLabel->setText(QString("Interval %1/%2").arg(m_activeFitSlot + 1).arg(n));
}

void MainWindow::onApplyFitRangeEntry() {
    if (!m_fitVLines) {
        QMessageBox::information(this, "No range placed", "Enable the fit and click on the plot to place the range first.");
        return;
    }
    bool ok1, ok2;
    double xmin = m_fitXminEdit->text().toDouble(&ok1);
    double xmax = m_fitXmaxEdit->text().toDouble(&ok2);
    if (!ok1 || !ok2) { QMessageBox::critical(this, "Invalid input", "Fit range values must be numbers."); return; }
    if (xmin > xmax) std::swap(xmin, xmax);
    m_fitVLines->setAllX({ xmin, xmax });
}

QVector<CurveKey> MainWindow::selectedFitColumns() const {
    QVector<CurveKey> out;
    for (int row = 0; row < m_fitColList->count(); ++row)
        if (m_fitColList->item(row)->isSelected() && row < m_currentYCols.size())
            out << m_currentYCols[row];
    return out;
}

void MainWindow::onFitColumnSelectionChanged() {
    if (m_activeFitSlot >= 0) computeAndDrawFit();
}

void MainWindow::clearFitResultsText() { m_fitResultsText->setPlainText(""); }

void MainWindow::populateFitAndIntegralLists() {
    rebuildFitListWidget();
    rebuildIntegralListWidget();
}

void MainWindow::rebuildFitListWidget() {
    m_fitColList->blockSignals(true);
    m_fitColList->clear();
    for (const auto &key : m_currentYCols) {
        auto *item = new QListWidgetItem(curveLabel(key));
        item->setForeground(QBrush(m_datasets[key.dsIdx].color));
        m_fitColList->addItem(item);
        item->setSelected(true);
    }
    m_fitColList->blockSignals(false);
}

void MainWindow::rebuildIntegralListWidget() {
    QVector<QPair<CurveKey, QString>> prevSel;
    for (int row = 0; row < m_integralList->count(); ++row)
        if (m_integralList->item(row)->isSelected() && row < m_integralItems.size())
            prevSel << m_integralItems[row];
    bool hadItems = !m_integralItems.isEmpty();

    m_integralList->blockSignals(true);
    m_integralList->clear();
    m_integralItems.clear();

    for (const auto &key : m_currentYCols) {
        auto *item = new QListWidgetItem(curveLabel(key));
        item->setForeground(QBrush(m_datasets[key.dsIdx].color));
        m_integralList->addItem(item);
        m_integralItems.push_back({ key, "data" });
    }
    bool multiSlot = m_fitSlots.size() > 1;
    for (const auto &key : m_currentYCols) {
        QVector<int> slotIdxs;
        for (auto it = m_fitResults.constBegin(); it != m_fitResults.constEnd(); ++it)
            if (it.key().first == key) slotIdxs << it.key().second;
        std::sort(slotIdxs.begin(), slotIdxs.end());
        for (int slotIdx : slotIdxs) {
            QString suffix = multiSlot ? QString(" (fit %1)").arg(slotIdx + 1) : " (fit)";
            auto *item = new QListWidgetItem(curveLabel(key) + suffix);
            item->setForeground(QBrush(m_datasets[key.dsIdx].color));
            m_integralList->addItem(item);
            m_integralItems.push_back({ key, QString("fit:%1").arg(slotIdx) });
        }
    }

    for (int row = 0; row < m_integralItems.size(); ++row) {
        const auto &it = m_integralItems[row];
        if (!hadItems) {
            if (it.second == "data") m_integralList->item(row)->setSelected(true);
        } else if (prevSel.contains(it)) {
            m_integralList->item(row)->setSelected(true);
        }
    }
    m_integralList->blockSignals(false);
}

void MainWindow::computeAndDrawFit() {
    if (!m_fitVLines || m_activeFitSlot < 0 || m_datasets.isEmpty() || m_currentYCols.isEmpty()) return;

    int idx = m_activeFitSlot;
    auto b = m_fitVLines->bounds();
    double xmin = b.first, xmax = b.second;
    m_fitXminEdit->setText(QString::number(xmin, 'g', 6));
    m_fitXmaxEdit->setText(QString::number(xmax, 'g', 6));
    m_fitSlots[idx].xmin = xmin;
    m_fitSlots[idx].xmax = xmax;

    for (const auto &yCol : m_currentYCols) {
        FitResultKey key(yCol, idx);
        if (m_fitGraphs.contains(key)) { m_plot->removePlottable(m_fitGraphs[key]); m_fitGraphs.remove(key); }
        m_fitResults.remove(key);
    }

    QVector<CurveKey> selectedCols = selectedFitColumns();
    m_fitSlots[idx].cols = selectedCols;

    QStringList linesOut;
    linesOut << QString("Interval %1/%2").arg(idx + 1).arg(m_fitSlots.size())
             << QString("Range: x in [%1, %2]").arg(QString::number(xmin, 'g', 6), QString::number(xmax, 'g', 6)) << "";
    if (selectedCols.isEmpty()) linesOut << "No columns selected -- pick some in the list above.";

    FitOptions opts;
    QString t = m_fitTypeCombo->currentText();
    if (t == "Linear") opts.kind = FitKind::Linear;
    else if (t == "Polynomial") { opts.kind = FitKind::Polynomial; opts.polyDegree = m_polyDegreeSpin->value(); }
    else if (t.startsWith("Exponential")) opts.kind = FitKind::Exponential;
    else if (t.startsWith("Logarithmic")) opts.kind = FitKind::Logarithmic;
    else if (t.startsWith("Power")) opts.kind = FitKind::Power;
    else {
        opts.kind = FitKind::Custom;
        opts.customExpr = m_customExprEdit->text().trimmed();
        for (const QString &p : m_customParamsEdit->text().split(','))
            if (!p.trimmed().isEmpty()) opts.customParamNames << p.trimmed();
        QString guessTxt = m_customGuessEdit->text().trimmed();
        if (!guessTxt.isEmpty()) {
            for (const QString &g : guessTxt.split(',')) {
                bool ok = false;
                double v = g.trimmed().toDouble(&ok);
                if (!ok) {
                    linesOut << QString("Invalid initial guess '%1' -- enter numbers separated by commas.").arg(g.trimmed());
                    m_fitResultsText->setPlainText(linesOut.join("\n"));
                    m_plot->replot();
                    return;
                }
                opts.customGuesses << v;
            }
        } else {
            for (int i = 0; i < opts.customParamNames.size(); ++i) opts.customGuesses << 1.0;
        }
    }

    for (const auto &yCol : selectedCols) {
        QString label = curveLabel(yCol);
        QVector<double> xFull = curveXArray(yCol);
        QVector<double> yFull = curveYArray(yCol);
        QVector<double> xs, ys;
        int n = std::min(xFull.size(), yFull.size());
        for (int i = 0; i < n; ++i) {
            if (xFull[i] >= xmin && xFull[i] <= xmax && !std::isnan(xFull[i]) && !std::isnan(yFull[i])) {
                xs << xFull[i]; ys << yFull[i];
            }
        }
        if (xs.size() < 2) { linesOut << QString("%1: not enough points in range").arg(label); continue; }

        FitResult result;
        try {
            result = performFit(xs, ys, xmin, xmax, opts);
        } catch (std::exception &e) {
            linesOut << QString("%1: fit failed -- %2").arg(label, e.what());
            continue;
        }

        double ssRes = 0, ssTot = 0, meanY = 0;
        for (double y : result.ysUsed) meanY += y;
        meanY /= result.ysUsed.isEmpty() ? 1 : result.ysUsed.size();
        for (int i = 0; i < result.xsUsed.size(); ++i) {
            double pred = result.func(result.xsUsed[i]);
            ssRes += (result.ysUsed[i] - pred) * (result.ysUsed[i] - pred);
            ssTot += (result.ysUsed[i] - meanY) * (result.ysUsed[i] - meanY);
        }
        double r2 = ssTot != 0 ? 1.0 - ssRes / ssTot : std::numeric_limits<double>::quiet_NaN();

        if (result.plotXmin >= result.plotXmax) { linesOut << QString("%1: no valid x-range to draw this fit").arg(label); continue; }

        QVector<double> xGrid(200), yGrid(200);
        for (int i = 0; i < 200; ++i) {
            double x = result.plotXmin + (result.plotXmax - result.plotXmin) * i / 199.0;
            xGrid[i] = x;
            yGrid[i] = result.func(x);
        }

        QColor color = m_dataGraphs.contains(yCol) ? m_dataGraphs[yCol]->pen().color() : QColor("black");
        QCPGraph *fitGraph = m_plot->addGraph(m_plot->xAxis, valueAxisFor(yCol));
        fitGraph->setData(xGrid, yGrid, true);
        fitGraph->setSelectable(QCP::stNone);
        QPen pen(color); pen.setStyle(Qt::DashLine); pen.setWidthF(2.0);
        fitGraph->setPen(pen);
        fitGraph->setScatterStyle(QCPScatterStyle::ssNone);
        fitGraph->setName(label + " (fit)");
        m_fitGraphs[FitResultKey(yCol, idx)] = fitGraph;

        result.r2 = r2;
        result.n = result.xsUsed.size();
        m_fitResults[FitResultKey(yCol, idx)] = result;

        linesOut << QString("%1:\n  %2\n  R^2 = %3   (n=%4)")
                        .arg(label, result.formula, QString::number(r2, 'f', 5)).arg(result.n);
    }

    m_plot->replot();
    m_fitResultsText->setPlainText(linesOut.join("\n\n"));

    rebuildIntegralListWidget();
    if (m_integralEnableCheck->isChecked() && integralUsesFit()) computeAndDrawIntegral();
}

// ========================================================== integral UI
QGroupBox *MainWindow::buildIntegralBox() {
    auto *box = new QGroupBox("Integral (area under curve)");
    auto *layout = new QVBoxLayout(box);

    m_integralEnableCheck = new QCheckBox("Enable integral (click plot to place range)");
    connect(m_integralEnableCheck, &QCheckBox::toggled, this, &MainWindow::onIntegralEnableToggled);
    layout->addWidget(m_integralEnableCheck);

    auto *rangeRow = new QHBoxLayout;
    rangeRow->addWidget(new QLabel("Range:"));
    m_integralXminEdit = new QLineEdit;
    m_integralXminEdit->setPlaceholderText("xmin");
    m_integralXmaxEdit = new QLineEdit;
    m_integralXmaxEdit->setPlaceholderText("xmax");
    rangeRow->addWidget(m_integralXminEdit);
    rangeRow->addWidget(m_integralXmaxEdit);
    auto *applyBtn = new QPushButton("Apply");
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::onApplyIntegralRangeEntry);
    rangeRow->addWidget(applyBtn);
    layout->addLayout(rangeRow);

    auto *navRow = new QHBoxLayout;
    auto *prevBtn = new QPushButton(QString::fromUtf8("\u25c0"));
    prevBtn->setMaximumWidth(32);
    connect(prevBtn, &QPushButton::clicked, this, &MainWindow::onPrevIntegralInterval);
    navRow->addWidget(prevBtn);
    m_integralNavLabel = new QLabel;
    m_integralNavLabel->setAlignment(Qt::AlignCenter);
    navRow->addWidget(m_integralNavLabel, 1);
    auto *nextBtn = new QPushButton(QString::fromUtf8("\u25b6"));
    nextBtn->setMaximumWidth(32);
    connect(nextBtn, &QPushButton::clicked, this, &MainWindow::onNextIntegralInterval);
    navRow->addWidget(nextBtn);
    layout->addLayout(navRow);

    auto *newIntervalBtn = new QPushButton("+ New interval");
    connect(newIntervalBtn, &QPushButton::clicked, this, &MainWindow::onNewIntegralInterval);
    layout->addWidget(newIntervalBtn);

    layout->addWidget(new QLabel("Integrate (data and/or fit):"));
    m_integralList = new QListWidget;
    m_integralList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_integralList->setFixedHeight(140); // same height as the curve-fit list
    connect(m_integralList, &QListWidget::itemSelectionChanged, this, &MainWindow::onIntegralSelectionChanged);
    layout->addWidget(m_integralList);

    m_integralResultsText = new QTextEdit;
    m_integralResultsText->setReadOnly(true);
    m_integralResultsText->setMaximumHeight(110);
    layout->addWidget(m_integralResultsText);

    return box;
}

void MainWindow::clearIntegralResultsText() { m_integralResultsText->setPlainText(""); }

QVector<double> MainWindow::snapValuesForCurrentPlot() const {
    QVector<double> vals;
    for (const auto &key : m_currentYCols) {
        for (double x : curveXArray(key)) if (!std::isnan(x)) vals << x;
    }
    std::sort(vals.begin(), vals.end());
    vals.erase(std::unique(vals.begin(), vals.end()), vals.end());
    return vals;
}

void MainWindow::onIntegralEnableToggled(bool on) {
    if (on) {
        if (m_datasets.isEmpty() || m_currentYCols.isEmpty()) {
            QMessageBox::information(this, "No plot", "Plot some data first, then enable the integral.");
            m_integralEnableCheck->blockSignals(true);
            m_integralEnableCheck->setChecked(false);
            m_integralEnableCheck->blockSignals(false);
            return;
        }
        startIntegralPlacement();
    } else {
        cancelIntegralPlacement();
        removeIntegralDraggables();
        for (auto it = m_integralFills.begin(); it != m_integralFills.end(); ++it) m_plot->removePlottable(it.value());
        m_integralFills.clear();
        m_integralResults.clear();
        m_integralSlots.clear();
        m_activeIntegralSlot = -1;
        clearIntegralResultsText();
        updateIntegralNavLabel();
        m_plot->replot();
    }
}

void MainWindow::startIntegralPlacement() {
    removeIntegralDraggables();
    for (auto it = m_integralFills.begin(); it != m_integralFills.end(); ++it) m_plot->removePlottable(it.value());
    m_integralFills.clear();
    m_integralResults.clear();
    m_integralSlots.clear();
    m_activeIntegralSlot = -1;
    cancelIntegralPlacement();
    m_integralPendingXs.clear();
    m_integralPlacementActive = true;
    m_integralResultsText->setPlainText("Click on the plot to place the first integral-range line.");
    updateIntegralNavLabel();
}

void MainWindow::cancelIntegralPlacement() {
    m_integralPlacementActive = false;
    m_integralPendingXs.clear();
}

void MainWindow::onPlotClickedForIntegral(QMouseEvent *event) {
    if (!m_plot->axisRect()->rect().contains(event->pos())) return;
    double x = m_plot->xAxis->pixelToCoord(event->pos().x());
    m_integralPendingXs << x;
    if (m_integralPendingXs.size() < 2) {
        m_integralResultsText->setPlainText("Click on the plot to place the second integral-range line.");
        return;
    }
    double x0 = m_integralPendingXs[0], x1 = m_integralPendingXs[1];
    if (x0 > x1) std::swap(x0, x1);
    cancelIntegralPlacement();
    beginNewIntegralInterval(x0, x1);
}

void MainWindow::onNewIntegralInterval() {
    if (m_datasets.isEmpty() || m_currentYCols.isEmpty()) {
        QMessageBox::information(this, "No plot", "Plot some data first.");
        return;
    }
    if (!m_integralEnableCheck->isChecked()) {
        m_integralEnableCheck->blockSignals(true);
        m_integralEnableCheck->setChecked(true);
        m_integralEnableCheck->blockSignals(false);
    }
    removeIntegralDraggables();
    cancelIntegralPlacement();
    m_integralPendingXs.clear();
    m_integralPlacementActive = true;
    m_integralResultsText->setPlainText("Click on the plot to place the first integral-range line for the new interval.");
}

void MainWindow::beginNewIntegralInterval(double x0, double x1) {
    IntegralSlot slot;
    slot.xmin = x0; slot.xmax = x1;
    m_integralSlots.push_back(slot);
    m_activeIntegralSlot = m_integralSlots.size() - 1;
    showIntegralInterval(m_activeIntegralSlot);
}

void MainWindow::showIntegralInterval(int idx) {
    if (idx < 0 || idx >= m_integralSlots.size()) return;
    m_activeIntegralSlot = idx;
    removeIntegralDraggables();
    const IntegralSlot &slot = m_integralSlots[idx];
    m_integralXminEdit->setText(QString::number(slot.xmin, 'g', 6));
    m_integralXmaxEdit->setText(QString::number(slot.xmax, 'g', 6));

    m_integralVLines = new DraggableVLineGroup(
        m_plot, { slot.xmin, slot.xmax }, { QColor("seagreen"), QColor("seagreen") },
        [this]() { computeAndDrawIntegral(); },
        [this]() { return m_snapCheck->isChecked() ? snapValuesForCurrentPlot() : QVector<double>(); });

    m_integralList->blockSignals(true);
    for (int row = 0; row < m_integralList->count() && row < m_integralItems.size(); ++row) {
        bool sel = slot.items.isEmpty()
                       ? (m_integralItems[row].second == "data")
                       : slot.items.contains(m_integralItems[row]);
        m_integralList->item(row)->setSelected(sel);
    }
    m_integralList->blockSignals(false);

    updateIntegralNavLabel();
    computeAndDrawIntegral();
}

void MainWindow::removeIntegralDraggables() {
    if (m_integralVLines) { m_integralVLines->remove(); delete m_integralVLines; m_integralVLines = nullptr; }
}

void MainWindow::onPrevIntegralInterval() {
    if (m_integralSlots.isEmpty()) return;
    int idx = (m_activeIntegralSlot <= 0) ? m_integralSlots.size() - 1 : m_activeIntegralSlot - 1;
    showIntegralInterval(idx);
}

void MainWindow::onNextIntegralInterval() {
    if (m_integralSlots.isEmpty()) return;
    int idx = (m_activeIntegralSlot + 1) % m_integralSlots.size();
    showIntegralInterval(idx);
}

void MainWindow::updateIntegralNavLabel() {
    int n = m_integralSlots.size();
    if (n == 0) { m_integralNavLabel->setText(""); return; }
    if (m_activeIntegralSlot < 0) { m_integralNavLabel->setText(QString("Interval %1/%1 (placing...)").arg(n + 1)); return; }
    m_integralNavLabel->setText(QString("Interval %1/%2").arg(m_activeIntegralSlot + 1).arg(n));
}

void MainWindow::onApplyIntegralRangeEntry() {
    if (!m_integralVLines) {
        QMessageBox::information(this, "No range placed", "Enable the integral and click on the plot to place the range first.");
        return;
    }
    bool ok1, ok2;
    double xmin = m_integralXminEdit->text().toDouble(&ok1);
    double xmax = m_integralXmaxEdit->text().toDouble(&ok2);
    if (!ok1 || !ok2) { QMessageBox::critical(this, "Invalid input", "Integral range values must be numbers."); return; }
    if (xmin > xmax) std::swap(xmin, xmax);
    m_integralVLines->setAllX({ xmin, xmax });
}

bool MainWindow::integralUsesFit() const {
    if (m_activeIntegralSlot < 0 || m_activeIntegralSlot >= m_integralSlots.size()) return false;
    for (const auto &it : m_integralSlots[m_activeIntegralSlot].items)
        if (it.second.startsWith("fit:")) return true;
    return false;
}

static double linInterp(const QVector<double> &xs, const QVector<double> &ys, double x) {
    int n = xs.size();
    if (x <= xs.first()) return ys.first();
    if (x >= xs.last()) return ys.last();
    for (int i = 0; i < n - 1; ++i) {
        if (x >= xs[i] && x <= xs[i + 1]) {
            double t = (xs[i + 1] - xs[i]) != 0 ? (x - xs[i]) / (xs[i + 1] - xs[i]) : 0.0;
            return ys[i] + t * (ys[i + 1] - ys[i]);
        }
    }
    return ys.last();
}

IntegralResultData MainWindow::integrateData(const QVector<double> &xIn, const QVector<double> &yIn,
                                              double xmin, double xmax) const {
    QVector<QPair<double, double>> pts;
    int n = std::min(xIn.size(), yIn.size());
    for (int i = 0; i < n; ++i)
        if (!std::isnan(xIn[i]) && !std::isnan(yIn[i])) pts << qMakePair(xIn[i], yIn[i]);
    std::sort(pts.begin(), pts.end(), [](const QPair<double,double> &a, const QPair<double,double> &b) { return a.first < b.first; });
    if (pts.isEmpty()) throw std::runtime_error("no data");

    QVector<double> xs, ys;
    for (const auto &p : pts) { xs << p.first; ys << p.second; }

    double effMin = std::max(xmin, xs.first());
    double effMax = std::min(xmax, xs.last());
    if (effMin >= effMax) throw std::runtime_error("no data in range");

    QVector<double> outX, outY;
    outX << effMin; outY << linInterp(xs, ys, effMin);
    for (int i = 0; i < xs.size(); ++i)
        if (xs[i] > effMin && xs[i] < effMax) { outX << xs[i]; outY << ys[i]; }
    outX << effMax; outY << linInterp(xs, ys, effMax);

    double area = 0;
    for (int i = 0; i + 1 < outX.size(); ++i)
        area += 0.5 * (outY[i] + outY[i + 1]) * (outX[i + 1] - outX[i]);

    IntegralResultData r;
    r.area = area; r.xs = outX; r.ys = outY;
    return r;
}

void MainWindow::onIntegralSelectionChanged() {
    if (m_activeIntegralSlot >= 0) computeAndDrawIntegral();
}

void MainWindow::computeAndDrawIntegral() {
    if (!m_integralVLines || m_activeIntegralSlot < 0 || m_datasets.isEmpty() || m_currentYCols.isEmpty()) return;

    int idx = m_activeIntegralSlot;
    auto b = m_integralVLines->bounds();
    double xmin = b.first, xmax = b.second;
    m_integralXminEdit->setText(QString::number(xmin, 'g', 6));
    m_integralXmaxEdit->setText(QString::number(xmax, 'g', 6));
    m_integralSlots[idx].xmin = xmin;
    m_integralSlots[idx].xmax = xmax;

    QVector<QPair<CurveKey, QString>> selected;
    for (int row = 0; row < m_integralList->count() && row < m_integralItems.size(); ++row)
        if (m_integralList->item(row)->isSelected()) selected << m_integralItems[row];
    m_integralSlots[idx].items = selected;

    // remove this interval's previous fills
    QVector<IntegralResultKey> toRemove;
    for (auto it = m_integralFills.begin(); it != m_integralFills.end(); ++it)
        if (it.key().second == idx) toRemove << it.key();
    for (const auto &k : toRemove) { m_plot->removePlottable(m_integralFills[k]); m_integralFills.remove(k); m_integralResults.remove(k); }

    QStringList linesOut;
    for (const auto &item : selected) {
        const CurveKey &yCol = item.first;
        const QString &source = item.second;
        QString label = curveLabel(yCol);
        QColor color = m_dataGraphs.contains(yCol) ? m_dataGraphs[yCol]->pen().color() : QColor("steelblue");

        QVector<double> xPts, yPts;
        double area = 0;
        QString note;
        QString tag = label;

        if (source.startsWith("fit:")) {
            int fitSlot = source.mid(4).toInt();
            FitResultKey frk(yCol, fitSlot);
            if (!m_fitResults.contains(frk)) continue;
            const FitResult &fr = m_fitResults[frk];
            double effMin = std::max(xmin, fr.plotXmin);
            double effMax = std::min(xmax, fr.plotXmax);
            if (effMin >= effMax) { linesOut << QString("%1 (fit): no overlap with fit domain").arg(label); continue; }
            int nsamp = 200;
            for (int i = 0; i < nsamp; ++i) {
                double x = effMin + (effMax - effMin) * i / (nsamp - 1);
                xPts << x; yPts << fr.func(x);
            }
            for (int i = 0; i + 1 < xPts.size(); ++i) area += 0.5 * (yPts[i] + yPts[i + 1]) * (xPts[i + 1] - xPts[i]);
            tag = label + QString(" (fit %1)").arg(fitSlot + 1);
        } else {
            QVector<double> xFull = curveXArray(yCol), yFull = curveYArray(yCol);
            IntegralResultData r;
            try {
                r = integrateData(xFull, yFull, xmin, xmax);
            } catch (std::exception &e) {
                linesOut << QString("%1: %2").arg(label, e.what());
                continue;
            }
            xPts = r.xs; yPts = r.ys; area = r.area;
            bool clipped = (xPts.first() > xmin) || (xPts.last() < xmax);
            note = clipped ? "  (range clipped to data domain)" : "";
        }

        QVector<double> polyX = xPts, polyY = yPts;
        polyX << xPts.last() << xPts.first();
        polyY << 0.0 << 0.0;

        auto *curve = new QCPCurve(m_plot->xAxis, valueAxisFor(yCol));
        QVector<double> t(polyX.size());
        for (int i = 0; i < t.size(); ++i) t[i] = i;
        curve->setData(t, polyX, polyY);
        curve->setPen(Qt::NoPen);
        QColor fillColor = color; fillColor.setAlphaF(0.25);
        curve->setBrush(QBrush(fillColor));
        curve->setScatterStyle(QCPScatterStyle::ssNone);

        IntegralResultKey key(qMakePair(yCol, source), idx);
        m_integralFills[key] = curve;
        IntegralResultData stored; stored.area = area; stored.xs = xPts; stored.ys = yPts;
        m_integralResults[key] = stored;

        linesOut << QString::fromUtf8("%1: \u222b y dx \u2248 %2%3").arg(tag, QString::number(area, 'g', 6), note);
    }

    m_plot->replot();
    m_integralResultsText->setPlainText(linesOut.join("\n\n"));
}

// ======================================================= interpolation UI
QGroupBox *MainWindow::buildInterpBox() {
    auto *box = new QGroupBox("Interpolation cursor");
    auto *layout = new QVBoxLayout(box);

    m_interpEnableCheck = new QCheckBox("Enable cursor (click plot to place)");
    connect(m_interpEnableCheck, &QCheckBox::toggled, this, &MainWindow::onInterpEnableToggled);
    layout->addWidget(m_interpEnableCheck);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel("x ="));
    m_interpXEdit = new QLineEdit;
    row->addWidget(m_interpXEdit);
    auto *applyBtn = new QPushButton("Apply");
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::onApplyInterpEntry);
    row->addWidget(applyBtn);
    layout->addLayout(row);

    m_interpResultsText = new QTextEdit;
    m_interpResultsText->setReadOnly(true);
    m_interpResultsText->setMaximumHeight(110);
    layout->addWidget(m_interpResultsText);

    return box;
}

void MainWindow::clearInterpResultsText() { m_interpResultsText->setPlainText(""); }

void MainWindow::onInterpEnableToggled(bool on) {
    if (on) {
        if (m_datasets.isEmpty() || m_currentYCols.isEmpty()) {
            QMessageBox::information(this, "No plot", "Plot some data first, then enable interpolation.");
            m_interpEnableCheck->blockSignals(true);
            m_interpEnableCheck->setChecked(false);
            m_interpEnableCheck->blockSignals(false);
            return;
        }
        startInterpPlacement();
    } else {
        cancelInterpPlacement();
        removeInterpArtifacts();
        clearInterpResultsText();
        m_plot->replot();
    }
}

void MainWindow::startInterpPlacement() {
    removeInterpArtifacts();
    cancelInterpPlacement();
    m_interpPlacementActive = true;
    m_interpResultsText->setPlainText("Click on the plot to place the interpolation line.");
}

void MainWindow::cancelInterpPlacement() { m_interpPlacementActive = false; }

void MainWindow::onPlotClickedForInterp(QMouseEvent *event) {
    if (!m_plot->axisRect()->rect().contains(event->pos())) return;
    double x0 = m_plot->xAxis->pixelToCoord(event->pos().x());
    cancelInterpPlacement();
    m_interpVLine = new DraggableVLineGroup(
        m_plot, { x0 }, { QColor("#1f77b4") },
        [this]() { computeAndDrawInterp(); },
        [this]() { return m_snapCheck->isChecked() ? snapValuesForCurrentPlot() : QVector<double>(); });
    computeAndDrawInterp();
}

void MainWindow::onApplyInterpEntry() {
    if (!m_interpVLine) {
        QMessageBox::information(this, "No line placed", "Enable interpolation and click on the plot to place the line first.");
        return;
    }
    bool ok;
    double x0 = m_interpXEdit->text().toDouble(&ok);
    if (!ok) { QMessageBox::critical(this, "Invalid input", "Interpolation X position must be a number."); return; }
    m_interpVLine->setAllX({ x0 });
}

void MainWindow::removeInterpArtifacts() {
    if (m_interpVLine) { m_interpVLine->remove(); delete m_interpVLine; m_interpVLine = nullptr; }
    for (auto it = m_interpMarkers.begin(); it != m_interpMarkers.end(); ++it) m_plot->removePlottable(it.value());
    m_interpMarkers.clear();
    m_interpXEdit->setText("");
}

void MainWindow::computeAndDrawInterp() {
    if (!m_interpVLine || m_datasets.isEmpty() || m_currentYCols.isEmpty()) return;

    double x0 = m_interpVLine->allX().first();
    m_interpXEdit->setText(QString::number(x0, 'g', 6));

    QStringList linesOut;
    linesOut << QString("x = %1").arg(QString::number(x0, 'g', 6)) << "";

    for (const auto &yCol : m_currentYCols) {
        QString label = curveLabel(yCol);
        if (m_interpMarkers.contains(yCol)) { m_plot->removePlottable(m_interpMarkers[yCol]); m_interpMarkers.remove(yCol); }

        QVector<QPair<double, double>> pts;
        QVector<double> xFull = curveXArray(yCol), yFull = curveYArray(yCol);
        int n = std::min(xFull.size(), yFull.size());
        for (int i = 0; i < n; ++i)
            if (!std::isnan(xFull[i]) && !std::isnan(yFull[i])) pts << qMakePair(xFull[i], yFull[i]);
        if (pts.size() < 2) { linesOut << QString("%1: not enough data").arg(label); continue; }
        std::sort(pts.begin(), pts.end(), [](const QPair<double,double> &a, const QPair<double,double> &b){ return a.first < b.first; });
        QVector<double> xs, ys;
        for (const auto &p : pts) { xs << p.first; ys << p.second; }

        double yInterp = linInterp(xs, ys, x0);
        bool extrapolated = x0 < xs.first() || x0 > xs.last();

        QColor color = m_dataGraphs.contains(yCol) ? m_dataGraphs[yCol]->pen().color() : QColor("black");
        QCPGraph *marker = m_plot->addGraph(m_plot->xAxis, valueAxisFor(yCol));
        marker->setData({ x0 }, { yInterp });
        marker->setSelectable(QCP::stNone);
        marker->setLineStyle(QCPGraph::lsNone);
        marker->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssDisc, QPen(Qt::black), QBrush(color), 9));
        m_interpMarkers[yCol] = marker;

        QString note = extrapolated ? "  (extrapolated)" : "";
        linesOut << QString::fromUtf8("%1: y \u2248 %2%3").arg(label, QString::number(yInterp, 'g', 6), note);
    }

    m_plot->replot();
    m_interpResultsText->setPlainText(linesOut.join("\n"));
}

// ============================================================ axis limits
QGroupBox *MainWindow::buildAxisLimitsBox() {
    auto *box = new QGroupBox("Axis limits");
    auto *grid = new QGridLayout(box);

    grid->addWidget(new QLabel("X min"), 0, 0);
    m_xlimMinEdit = new QLineEdit;
    grid->addWidget(m_xlimMinEdit, 0, 1);
    grid->addWidget(new QLabel("X max"), 0, 2);
    m_xlimMaxEdit = new QLineEdit;
    grid->addWidget(m_xlimMaxEdit, 0, 3);

    grid->addWidget(new QLabel("Y min"), 1, 0);
    m_ylimMinEdit = new QLineEdit;
    grid->addWidget(m_ylimMinEdit, 1, 1);
    grid->addWidget(new QLabel("Y max"), 1, 2);
    m_ylimMaxEdit = new QLineEdit;
    grid->addWidget(m_ylimMaxEdit, 1, 3);

    // Second Y axis limits (only shown while the second axis is switched on)
    auto *y2minLbl = new QLabel("Y2 min");
    m_y2limMinEdit = new QLineEdit;
    auto *y2maxLbl = new QLabel("Y2 max");
    m_y2limMaxEdit = new QLineEdit;
    grid->addWidget(y2minLbl, 2, 0);
    grid->addWidget(m_y2limMinEdit, 2, 1);
    grid->addWidget(y2maxLbl, 2, 2);
    grid->addWidget(m_y2limMaxEdit, 2, 3);
    m_y2limWidgets = {y2minLbl, m_y2limMinEdit, y2maxLbl, m_y2limMaxEdit};
    for (QWidget *w : m_y2limWidgets) w->setVisible(false);

    auto *applyBtn = new QPushButton("Apply limits");
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::applyAxisLimits);
    grid->addWidget(applyBtn, 3, 0, 1, 2);
    auto *autoBtn = new QPushButton("Autoscale");
    connect(autoBtn, &QPushButton::clicked, this, &MainWindow::autoscaleAxes);
    grid->addWidget(autoBtn, 3, 2, 1, 2);

    return box;
}

// ================================================================= export
QGroupBox *MainWindow::buildExportBox() {
    auto *box = new QGroupBox("Export");
    auto *layout = new QVBoxLayout(box);

    auto *imgBtn = new QPushButton("Save plot image...");
    connect(imgBtn, &QPushButton::clicked, this, &MainWindow::exportImage);
    layout->addWidget(imgBtn);

    return box;
}

void MainWindow::exportImage() {
    if (m_datasets.isEmpty() || m_currentYCols.isEmpty()) {
        QMessageBox::information(this, "Nothing to export", "Plot some data first.");
        return;
    }
    QString startDir = m_lastSaveDir.isEmpty() ? QDir::homePath() : m_lastSaveDir;
    QString path = QFileDialog::getSaveFileName(this, "Save plot image", startDir + "/plot.png",
                                                  "PNG image (*.png);;PDF (*.pdf);;JPEG (*.jpg)");
    if (path.isEmpty()) return;
    setLastSaveDir(QFileInfo(path).absolutePath());
    bool ok = false;
    if (path.endsWith(".pdf", Qt::CaseInsensitive)) ok = m_plot->savePdf(path);
    else if (path.endsWith(".jpg", Qt::CaseInsensitive) || path.endsWith(".jpeg", Qt::CaseInsensitive))
        ok = m_plot->saveJpg(path);
    else ok = m_plot->savePng(path);
    if (!ok) QMessageBox::critical(this, "Export failed", "Could not save the image to that location.");
}

void MainWindow::exportLatex() {
    if (m_datasets.isEmpty() || m_currentYCols.isEmpty()) {
        QMessageBox::information(this, "Nothing to export", "Plot some data first.");
        return;
    }
    // "Reset settings" closes the dialog and asks for a fresh one, which starts from the defaults
    bool again = true;
    while (again) {
        LatexExportDialog dlg(this);
        dlg.exec();
        again = dlg.resetRequested();
    }
}

// ------------------------------------------------- public accessor helpers
QString MainWindow::plotTitleText() const {
    auto *elem = m_plot ? qobject_cast<QCPTextElement *>(m_plot->plotLayout()->elementAt(0)) : nullptr;
    return elem ? elem->text() : QString();
}

void MainWindow::currentXRange(double &lo, double &hi) const {
    if (m_plot) { lo = m_plot->xAxis->range().lower; hi = m_plot->xAxis->range().upper; }
    else { lo = 0; hi = 1; }
}

void MainWindow::currentYRange(double &lo, double &hi) const {
    if (m_plot) { lo = m_plot->yAxis->range().lower; hi = m_plot->yAxis->range().upper; }
    else { lo = 0; hi = 1; }
}
