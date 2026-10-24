#include "LatexExportDialog.h"
#include "MainWindow.h"
#include "LatexColors.h"
#include "ColorPickerWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QProcess>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QTimer>
#include <QFontMetrics>
#include <QSettings>
#include <QScreen>
#include <QScrollBar>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QToolButton>
#include <QFrame>
#include <QApplication>
#include <functional>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <memory>

namespace {
// Labels in the "Ticks & number format" and "Style" sections live in separate
// QGroupBoxes, each with its own QGridLayout. Left to themselves, each grid
// sizes its label column to only its own widest label, so the input boxes in
// one section don't line up with the ones in the other. Widening every label
// in both sections to a shared width keeps the two input columns aligned.
const QStringList &settingsLabelTexts() {
    static const QStringList texts = {
        "X tick step (blank = auto):", "Y tick step (blank = auto):",
        "X decimals:", "Y decimals:", "Tick alignment:",
        "X tick pos:", "Y tick pos:",
        "Grid:", "Smoothing:", "Width (cm):", "Height (cm):",
    };
    return texts;
}

int settingsLabelColumnWidth(const QWidget *sample) {
    QFontMetrics fm(sample ? sample->font() : QFont());
    int w = 0;
    for (const QString &t : settingsLabelTexts())
        w = std::max(w, fm.horizontalAdvance(t));
    return w + 6;
}

QLabel *makeSettingsLabel(const QString &text, int columnWidth) {
    auto *l = new QLabel(text);
    l->setMinimumWidth(columnWidth);
    return l;
}

double parseOptionalDouble(QLineEdit *edit, const QString &name, bool &has) {
    QString s = edit->text().trimmed();
    if (s.isEmpty()) { has = false; return 0.0; }
    bool ok = false;
    double v = s.toDouble(&ok);
    if (!ok) throw std::runtime_error((name + " must be a number").toStdString());
    has = true;
    return v;
}
int parseIntOr(QLineEdit *edit, int fallback) {
    bool ok = false;
    int v = edit->text().trimmed().toInt(&ok);
    return ok ? v : fallback;
}

// Integral result sources are "data" or "fit:<fitSlotIdx>" (see
// MainWindow::computeAndDrawIntegral). Returns the distinct sources present
// for one curve, "data" first, then fit sources in ascending slot order.
QVector<QString> distinctIntegralSources(const CurveKey &yCol, const QMap<IntegralResultKey, IntegralResultData> &results) {
    QSet<QString> set;
    for (auto it = results.constBegin(); it != results.constEnd(); ++it)
        if (it.key().first.first == yCol) set.insert(it.key().first.second);
    QVector<QString> sources(set.begin(), set.end());
    std::sort(sources.begin(), sources.end(), [](const QString &a, const QString &b) {
        if (a == "data" || b == "data") return a == "data";
        return a.mid(4).toInt() < b.mid(4).toInt();
    });
    return sources;
}

// -1 for the "data" source, otherwise the fit slot index encoded in "fit:N".
int fitSlotFromSource(const QString &source) {
    return source.startsWith("fit:") ? source.mid(4).toInt() : -1;
}
} // namespace

LatexExportDialog::LatexExportDialog(MainWindow *app, QWidget *parent)
    : QDialog(parent), m_app(app) {
    setWindowTitle("Export to LaTeX (pgfplots)");
    resize(820, 720);

    m_fitResults = app->fitEnabled() ? app->fitResultsPublic() : QMap<FitResultKey, FitResult>();
    m_hasFit = !m_fitResults.isEmpty();
    m_integralResults = app->integralEnabled() ? app->integralResultsPublic() : QMap<IntegralResultKey, IntegralResultData>();
    m_hasIntegral = !m_integralResults.isEmpty();

    switch (app->plottedType()) {
    case MainWindow::PlotType::Bar: m_plotType = "bar"; break;
    case MainWindow::PlotType::Histogram: m_plotType = "hist"; break;
    default: m_plotType = "line"; break;
    }
    const bool lineType = (m_plotType == "line");

    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);

    bodyLayout->addWidget(buildSourceSection());
    bodyLayout->addWidget(buildLabelsLimitsSection());
    bodyLayout->addWidget(buildTicksSection());
    bodyLayout->addWidget(buildStyleSection());
    bodyLayout->addWidget(buildLegendSection());
    bodyLayout->addWidget(buildCurvesSection());
    bodyLayout->addWidget(buildErrorBarsSection());
    bodyLayout->addWidget(buildBarSection());
    // curve fit, integral and extra functions only exist for line plots
    for (QGroupBox *sec : { buildFitSection(), buildIntegralSection(), buildExtraFunctionSection() }) {
        sec->setVisible(lineType);
        bodyLayout->addWidget(sec);
    }
    bodyLayout->addWidget(buildFigureSection());
    bodyLayout->addStretch();

    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    auto *btnRow = new QHBoxLayout;
    auto *genBtn = new QPushButton("Generate LaTeX Code");
    connect(genBtn, &QPushButton::clicked, this, &LatexExportDialog::generate);
    btnRow->addWidget(genBtn);
    auto *cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);
    auto *resetBtn = new QPushButton("Reset settings");
    resetBtn->setToolTip(QString("Set every setting of this dialog back to its default value.\n"
                                 "Only the settings of the current plot type (%1) are reset.")
                             .arg(m_plotType == "line" ? "line / scatter" : m_plotType == "bar" ? "bar chart" : "histogram"));
    connect(resetBtn, &QPushButton::clicked, this, &LatexExportDialog::resetSettings);
    btnRow->addWidget(resetBtn);
    outer->addLayout(btnRow);

    if (m_plotType == "hist") {
        // A histogram is exported as its bins (already computed), which are small: embed them.
        m_sourceEmbedRadio->setChecked(true);
        m_sourceCsvRadio->setEnabled(false);
        m_sourceCsvRadio->setToolTip("Histograms are exported as their binned values, embedded in the LaTeX code");
    }
    if (m_plotType == "bar" && app->barNumbersAsText()) {
        // the bar positions (0, 1, 2, ...) are not a column of the data file, so the values are embedded
        m_sourceEmbedRadio->setChecked(true);
        m_sourceCsvRadio->setEnabled(false);
        m_sourceCsvRadio->setToolTip("Not available while X numbers are treated as text: the bars are exported as embedded values");
    }
    loadSettings();
    updateZeroFillEnabled();
    connect(this, &QDialog::finished, this, &LatexExportDialog::saveSettings);

    onSourceChanged();
    onLegendModeChanged();
    onWrapFigureChanged();

    // Open wide enough that the widest row (curve settings etc.) fits without having to
    // widen the window every time. A size you chose earlier is kept if it is larger.
    body->adjustSize();
    int wantW = body->sizeHint().width() + scroll->verticalScrollBar()->sizeHint().width() + 60;
    int wantH = 800;
    QSettings sizeSettings;
    QSize saved = sizeSettings.value("latexExport/dialogSize").toSize();
    if (saved.isValid()) { wantW = std::max(wantW, saved.width()); wantH = saved.height(); }
    wantW = std::max(wantW, 1000);
    if (QScreen *scr = QGuiApplication::primaryScreen()) {
        QRect avail = scr->availableGeometry();
        wantW = std::min(wantW, int(avail.width() * 0.95));
        wantH = std::min(wantH, int(avail.height() * 0.92));
    }
    resize(wantW, wantH);
    connect(this, &QDialog::finished, this, [this]() {
        QSettings().setValue("latexExport/dialogSize", size());
    });
}

// ---------------------------------------------------------------- source
QGroupBox *LatexExportDialog::buildSourceSection() {
    auto *box = new QGroupBox("Data source");
    auto *layout = new QVBoxLayout(box);

    m_sourceEmbedRadio = new QRadioButton("Embed data points directly in the code");
    m_sourceEmbedRadio->setChecked(true);
    layout->addWidget(m_sourceEmbedRadio);
    m_sourceCsvRadio = new QRadioButton(QString::fromUtf8("Load from CSV file(s) (\\pgfplotstable, table=)"));
    layout->addWidget(m_sourceCsvRadio);
    connect(m_sourceEmbedRadio, &QRadioButton::toggled, this, &LatexExportDialog::onSourceChanged);

    m_csvFrame = new QWidget;
    auto *csvLayout = new QVBoxLayout(m_csvFrame);
    csvLayout->setContentsMargins(16, 4, 0, 0);

    bool multi = false;
    {
        QSet<int> ds;
        for (const auto &k : m_app->currentYCols()) ds.insert(k.dsIdx);
        multi = ds.size() > 1;
    }
    QString intro = multi
        ? "Each source file below is exported as its own CSV next to the .tex file, "
          "and every curve's \\addplot references the CSV of the file its column came from."
        : "Filename and column separator are pre-filled from the file you already loaded (auto-detected).";
    intro += " Only change them if you'll place a renamed or differently-formatted copy next to the .tex file.";
    auto *introLabel = new QLabel(intro);
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet("color:#555;");
    csvLayout->addWidget(introLabel);

    static const QMap<QString, QString> sepMap = { {",", "comma"}, {";", "semicolon"}, {"\t", "tab"}, {"\\s+", "space"} };
    QVector<int> usedDsIdxs;
    { QSet<int> s; for (const auto &k : m_app->currentYCols()) s.insert(k.dsIdx); usedDsIdxs = QVector<int>(s.begin(), s.end()); std::sort(usedDsIdxs.begin(), usedDsIdxs.end()); }

    bool anyCommaDecimal = false;
    for (int dsIdx : usedDsIdxs) {
        const Dataset &ds = m_app->datasets()[dsIdx];
        auto *row = new QWidget;
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 2, 0, 2);

        QPixmap swatch(12, 12);
        swatch.fill(ds.color);
        auto *swLabel = new QLabel;
        swLabel->setPixmap(swatch);
        rl->addWidget(swLabel);

        if (multi) { auto *nameLabel = new QLabel(ds.basename); nameLabel->setMinimumWidth(100); rl->addWidget(nameLabel); }

        rl->addWidget(new QLabel("Filename:"));
        auto *filenameEdit = new QLineEdit(ds.basename);
        filenameEdit->setMaximumWidth(160);
        rl->addWidget(filenameEdit);

        rl->addWidget(new QLabel("Col sep:"));
        auto *sepCombo = new QComboBox;
        sepCombo->addItems({"comma", "semicolon", "tab", "space"});
        sepCombo->setCurrentText(sepMap.value(ds.info.sep, "comma"));
        sepCombo->setMaximumWidth(100);
        rl->addWidget(sepCombo);
        rl->addStretch();

        csvLayout->addWidget(row);
        m_csvDsRows[dsIdx] = { filenameEdit, sepCombo };

        if (ds.info.decimal != "." && !ds.info.decimal.isEmpty()) anyCommaDecimal = true;
    }
    if (anyCommaDecimal) {
        auto *note = new QLabel(
            "Note: at least one source file uses a comma as the decimal mark, but pgfplots table "
            "reading expects a dot -- the CSV(s) this dialog generates are always written with dot "
            "decimals, so this only matters if you swap in your own copy of the original file instead.");
        note->setWordWrap(true);
        note->setStyleSheet("color:#555;");
        csvLayout->addWidget(note);
    }

    layout->addWidget(m_csvFrame);
    return box;
}

void LatexExportDialog::onSourceChanged() {
    m_csvFrame->setVisible(m_sourceCsvRadio->isChecked());
}

// -------------------------------------------------------- labels & limits
QGroupBox *LatexExportDialog::buildLabelsLimitsSection() {
    auto *box = new QGroupBox("Labels & axis limits");
    auto *grid = new QGridLayout(box);

    grid->addWidget(new QLabel("Title:"), 0, 0);
    m_titleEdit = new QLineEdit(m_app->plotTitleText());
    grid->addWidget(m_titleEdit, 0, 1, 1, 3);

    grid->addWidget(new QLabel("X label:"), 1, 0);
    m_xlabelEdit = new QLineEdit(m_app->xAxisLabelText());
    grid->addWidget(m_xlabelEdit, 1, 1, 1, 3);

    grid->addWidget(new QLabel("Y label:"), 2, 0);
    m_ylabelEdit = new QLineEdit(m_app->yAxisLabelText());
    grid->addWidget(m_ylabelEdit, 2, 1, 1, 3);

    double xlo, xhi, ylo, yhi;
    m_app->currentXRange(xlo, xhi);
    m_app->currentYRange(ylo, yhi);

    grid->addWidget(new QLabel("X min:"), 3, 0);
    m_xminEdit = new QLineEdit(QString::number(xlo, 'g', 6));
    grid->addWidget(m_xminEdit, 3, 1);
    grid->addWidget(new QLabel("X max:"), 3, 2);
    m_xmaxEdit = new QLineEdit(QString::number(xhi, 'g', 6));
    grid->addWidget(m_xmaxEdit, 3, 3);

    grid->addWidget(new QLabel("Y min:"), 4, 0);
    m_yminEdit = new QLineEdit(QString::number(ylo, 'g', 6));
    grid->addWidget(m_yminEdit, 4, 1);
    grid->addWidget(new QLabel("Y max:"), 4, 2);
    m_ymaxEdit = new QLineEdit(QString::number(yhi, 'g', 6));
    grid->addWidget(m_ymaxEdit, 4, 3);

    m_logxCheck = new QCheckBox("Log X");
    m_logxCheck->setChecked(m_app->logXEnabled());
    grid->addWidget(m_logxCheck, 5, 0);
    m_logyCheck = new QCheckBox("Log Y");
    m_logyCheck->setChecked(m_app->logYEnabled());
    grid->addWidget(m_logyCheck, 5, 1);

    grid->addWidget(new QLabel("Y label rotation (blank = default):"), 6, 0, 1, 2);
    m_ylabelRotateEdit = new QLineEdit;
    grid->addWidget(m_ylabelRotateEdit, 6, 2);

    m_escapeTextCheck = new QCheckBox("Escape special characters in titles, labels and legend entries ($ _ & % # { } ~ ^ \\ get a backslash)");
    m_escapeTextCheck->setToolTip("Off: text is written to the LaTeX code exactly as typed, so you can use math like $\\alpha_1$.\n"
                                  "On: plain text is made LaTeX-safe by adding backslashes.");
    grid->addWidget(m_escapeTextCheck, 7, 0, 1, 4);

    // ---- second Y axis controls (only when curves are plotted on it) ----
    m_hasSecondAxis = m_app->secondAxisInUse();
    if (m_hasSecondAxis) {
        grid->addWidget(new QLabel("Y2 label:"), 8, 0);
        m_y2labelEdit = new QLineEdit(m_app->y2AxisLabelText());
        grid->addWidget(m_y2labelEdit, 8, 1, 1, 3);

        double y2lo, y2hi;
        m_app->currentY2Range(y2lo, y2hi);
        grid->addWidget(new QLabel("Y2 min:"), 9, 0);
        m_y2minEdit = new QLineEdit(QString::number(y2lo, 'g', 6));
        grid->addWidget(m_y2minEdit, 9, 1);
        grid->addWidget(new QLabel("Y2 max:"), 9, 2);
        m_y2maxEdit = new QLineEdit(QString::number(y2hi, 'g', 6));
        grid->addWidget(m_y2maxEdit, 9, 3);

        m_logy2Check = new QCheckBox("Log Y2");
        m_logy2Check->setChecked(m_app->logY2Enabled());
        grid->addWidget(m_logy2Check, 10, 0);

        grid->addWidget(new QLabel("Y2 label rotation (blank = default):"), 11, 0, 1, 2);
        m_y2labelRotateEdit = new QLineEdit;
        grid->addWidget(m_y2labelRotateEdit, 11, 2);
    }

    return box;
}

// -------------------------------------------------------------- ticks
QGroupBox *LatexExportDialog::buildTicksSection() {
    auto *box = new QGroupBox("Ticks & number format");
    auto *grid = new QGridLayout(box);
    int labelW = settingsLabelColumnWidth(box);
    m_hasSecondAxis = m_app->secondAxisInUse();
    int r = 0;

    grid->addWidget(makeSettingsLabel("X tick step (blank = auto):", labelW), r, 0);
    m_xtickEdit = new QLineEdit;
    m_xtickEdit->setMaximumWidth(70);
    grid->addWidget(m_xtickEdit, r++, 1, Qt::AlignLeft);
    grid->addWidget(makeSettingsLabel("Y tick step (blank = auto):", labelW), r, 0);
    m_ytickEdit = new QLineEdit;
    m_ytickEdit->setMaximumWidth(70);
    grid->addWidget(m_ytickEdit, r++, 1, Qt::AlignLeft);
    if (m_hasSecondAxis) {
        grid->addWidget(makeSettingsLabel("Y2 tick step (blank = auto):", labelW), r, 0);
        m_y2tickEdit = new QLineEdit;
        m_y2tickEdit->setMaximumWidth(70);
        grid->addWidget(m_y2tickEdit, r++, 1, Qt::AlignLeft);
    }

    // one row per axis: decimals (cols 1-2) and that axis' minor ticks (cols 3-4)
    auto addDecimalsRow = [&](const QString &text, QSpinBox *&spin, QCheckBox *&defCheck,
                              QCheckBox *&minorCheck, QSpinBox *&minorSpin,
                              QLineEdit *&rotEdit, QComboBox *&anchorCombo) {
        grid->addWidget(makeSettingsLabel(text, labelW), r, 0);
        spin = new QSpinBox;
        spin->setRange(0, 10);
        spin->setValue(2);
        spin->setMaximumWidth(70);
        grid->addWidget(spin, r, 1, Qt::AlignLeft);
        defCheck = new QCheckBox("Default");
        defCheck->setToolTip("Use the default number of decimals (no precision is written to the LaTeX code)");
        defCheck->setChecked(true);
        grid->addWidget(defCheck, r, 2, Qt::AlignLeft);
        connect(defCheck, &QCheckBox::toggled, this, &LatexExportDialog::updateZeroFillEnabled);

        minorCheck = new QCheckBox("Minor ticks");
        minorCheck->setToolTip("Draw minor ticks on this axis");
        grid->addWidget(minorCheck, r, 3, Qt::AlignLeft);
        minorSpin = new QSpinBox;
        minorSpin->setRange(1, 20);
        minorSpin->setValue(3);
        minorSpin->setMaximumWidth(70);
        minorSpin->setToolTip("Minor ticks between two major ticks");
        grid->addWidget(minorSpin, r, 4, Qt::AlignLeft);
        connect(minorCheck, &QCheckBox::toggled, this, &LatexExportDialog::updateMinorTickEnabled);

        // tick label rotation and anchor of this axis (cols 5-8)
        grid->addWidget(new QLabel("Label rotation (deg):"), r, 5, Qt::AlignRight);
        rotEdit = new QLineEdit;
        rotEdit->setMaximumWidth(60);
        rotEdit->setPlaceholderText("none");
        rotEdit->setToolTip("Rotates the tick labels of this axis. Blank = no rotation.");
        grid->addWidget(rotEdit, r, 6, Qt::AlignLeft);
        grid->addWidget(new QLabel("Anchor:"), r, 7, Qt::AlignRight);
        anchorCombo = new QComboBox;
        anchorCombo->addItems({"default", "east", "west", "north", "south", "north east", "north west",
                               "south east", "south west", "center", "base", "base east", "base west"});
        anchorCombo->setMaximumWidth(130);
        anchorCombo->setToolTip("Which point of the label sits on the tick. With a rotation of about 45 "
                                "degrees, 'east' (or 'north east') keeps long labels tidy.");
        grid->addWidget(anchorCombo, r, 8, Qt::AlignLeft);
        ++r;
    };
    addDecimalsRow("X decimals:", m_xdecSpin, m_xdecDefaultCheck, m_xMinorCheck, m_xMinorNum, m_xLabelRotEdit, m_xLabelAnchorCombo);
    addDecimalsRow("Y decimals:", m_ydecSpin, m_ydecDefaultCheck, m_yMinorCheck, m_yMinorNum, m_yLabelRotEdit, m_yLabelAnchorCombo);
    if (m_hasSecondAxis) addDecimalsRow("Y2 decimals:", m_y2decSpin, m_y2decDefaultCheck, m_y2MinorCheck, m_y2MinorNum, m_y2LabelRotEdit, m_y2LabelAnchorCombo);

    // shared minor-tick setting: when ticked it drives every axis and greys out the per-axis boxes above
    m_minorSameCheck = new QCheckBox("Same minor ticks on all axes");
    m_minorSameCheck->setChecked(true);
    m_minorSameCheck->setToolTip("Ticked: the setting to the right applies to every axis and the per-axis minor tick options are greyed out.\n"
                                 "Unticked: set the minor ticks of X, Y (and Y2) separately.");
    grid->addWidget(m_minorSameCheck, r, 0, 1, 3);
    m_minorCheck = new QCheckBox("Minor ticks");
    m_minorCheck->setToolTip("Draw minor ticks on all axes");
    grid->addWidget(m_minorCheck, r, 3, Qt::AlignLeft);
    m_minorNumCombo = new QSpinBox;
    m_minorNumCombo->setRange(1, 20);
    m_minorNumCombo->setValue(3);
    m_minorNumCombo->setMaximumWidth(70);
    m_minorNumCombo->setToolTip("Minor ticks between two major ticks");
    grid->addWidget(m_minorNumCombo, r++, 4, Qt::AlignLeft);
    connect(m_minorSameCheck, &QCheckBox::toggled, this, &LatexExportDialog::updateMinorTickEnabled);
    connect(m_minorCheck, &QCheckBox::toggled, this, &LatexExportDialog::updateMinorTickEnabled);
    updateMinorTickEnabled();

    grid->addWidget(makeSettingsLabel("Tick alignment:", labelW), r, 0);
    m_tickAlignCombo = new QComboBox;
    m_tickAlignCombo->addItems({"outside", "inside", "center"});
    m_tickAlignCombo->setMaximumWidth(110);
    grid->addWidget(m_tickAlignCombo, r++, 1, Qt::AlignLeft);

    m_zeroFillCheck = new QCheckBox("Zero-fill decimals (e.g. 1.20 not 1.2)");
    m_zeroFillCheck->setToolTip("Only applies to axes whose decimals are not set to Default");
    grid->addWidget(m_zeroFillCheck, r++, 0, 1, 2);

    m_hideTicksCheck = new QCheckBox("Hide all ticks (overrides tick step/decimals above)");
    grid->addWidget(m_hideTicksCheck, r++, 0, 1, 2);

    grid->addWidget(makeSettingsLabel("X tick pos:", labelW), r, 0);
    m_xtickPosCombo = new QComboBox;
    m_xtickPosCombo->addItems({"default", "bottom", "top", "both"});
    m_xtickPosCombo->setMaximumWidth(110);
    grid->addWidget(m_xtickPosCombo, r++, 1, Qt::AlignLeft);
    grid->addWidget(makeSettingsLabel("Y tick pos:", labelW), r, 0);
    m_ytickPosCombo = new QComboBox;
    m_ytickPosCombo->addItems({"default", "left", "right", "both"});
    m_ytickPosCombo->setMaximumWidth(110);
    grid->addWidget(m_ytickPosCombo, r++, 1, Qt::AlignLeft);

    grid->setColumnStretch(9, 1);   // spare column: keeps the per-axis controls packed to the left
    return box;
}

// -------------------------------------------------------------- style
QGroupBox *LatexExportDialog::buildStyleSection() {
    auto *box = new QGroupBox("Style");
    auto *grid = new QGridLayout(box);
    int labelW = settingsLabelColumnWidth(box);

    grid->addWidget(makeSettingsLabel("Grid:", labelW), 0, 0);
    m_gridModeCombo = new QComboBox;
    m_gridModeCombo->addItems({"none", "major", "both"});
    m_gridModeCombo->setCurrentText(m_app->gridEnabled() ? "major" : "none");
    m_gridModeCombo->setMaximumWidth(110);
    grid->addWidget(m_gridModeCombo, 0, 1, Qt::AlignLeft);

    grid->addWidget(makeSettingsLabel("Smoothing:", labelW), 1, 0);
    m_smoothingCombo = new QComboBox;
    m_smoothingCombo->addItems({"none", "smooth", "sharp plot"});
    m_smoothingCombo->setMaximumWidth(110);
    grid->addWidget(m_smoothingCombo, 1, 1, Qt::AlignLeft);

    grid->addWidget(makeSettingsLabel("Width (cm):", labelW), 2, 0);
    m_widthEdit = new QLineEdit("10");
    m_widthEdit->setMaximumWidth(70);
    grid->addWidget(m_widthEdit, 2, 1, Qt::AlignLeft);
    grid->addWidget(makeSettingsLabel("Height (cm):", labelW), 3, 0);
    m_heightEdit = new QLineEdit("7");
    m_heightEdit->setMaximumWidth(70);
    grid->addWidget(m_heightEdit, 3, 1, Qt::AlignLeft);

    m_axisBoxCheck = new QCheckBox("Box axis (draw all four sides, not just left+bottom)");
    grid->addWidget(m_axisBoxCheck, 4, 0, 1, 2);

    grid->setColumnStretch(2, 1);
    return box;
}

// -------------------------------------------------------------- legend
QGroupBox *LatexExportDialog::buildLegendSection() {
    auto *box = new QGroupBox("Legend");
    auto *layout = new QVBoxLayout(box);

    m_legendNoneRadio = new QRadioButton("No legend");
    m_legendPresetRadio = new QRadioButton("Preset corner position");
    m_legendPresetRadio->setChecked(true);
    m_legendCustomRadio = new QRadioButton("Exact position (axis coordinates 0-1)");
    layout->addWidget(m_legendNoneRadio);
    layout->addWidget(m_legendPresetRadio);
    layout->addWidget(m_legendCustomRadio);
    connect(m_legendNoneRadio, &QRadioButton::toggled, this, &LatexExportDialog::onLegendModeChanged);
    connect(m_legendPresetRadio, &QRadioButton::toggled, this, &LatexExportDialog::onLegendModeChanged);
    connect(m_legendCustomRadio, &QRadioButton::toggled, this, &LatexExportDialog::onLegendModeChanged);

    m_legendPresetFrame = new QWidget;
    auto *presetLayout = new QHBoxLayout(m_legendPresetFrame);
    presetLayout->setContentsMargins(16, 2, 0, 0);
    presetLayout->addWidget(new QLabel("Position:"));
    m_legendPosCombo = new QComboBox;
    m_legendPosCombo->addItems({"north east", "north west", "south east", "south west",
                                 "outer north east", "north", "south"});
    m_legendPosCombo->setMaximumWidth(150);
    presetLayout->addWidget(m_legendPosCombo);
    presetLayout->addStretch();
    layout->addWidget(m_legendPresetFrame);

    m_legendCustomFrame = new QWidget;
    auto *customLayout = new QGridLayout(m_legendCustomFrame);
    customLayout->setContentsMargins(16, 2, 0, 0);
    customLayout->addWidget(new QLabel("X:"), 0, 0);
    m_legendXEdit = new QLineEdit("0.5");
    m_legendXEdit->setMaximumWidth(60);
    customLayout->addWidget(m_legendXEdit, 0, 1);
    customLayout->addWidget(new QLabel("Y:"), 0, 2);
    m_legendYEdit = new QLineEdit("-0.15");
    m_legendYEdit->setMaximumWidth(60);
    customLayout->addWidget(m_legendYEdit, 0, 3);
    customLayout->addWidget(new QLabel("Anchor:"), 1, 0);
    m_legendAnchorCombo = new QComboBox;
    m_legendAnchorCombo->addItems({"north", "south", "east", "west", "north east", "north west", "south east", "south west", "center"});
    m_legendAnchorCombo->setMaximumWidth(150);
    customLayout->addWidget(m_legendAnchorCombo, 1, 1, 1, 3);
    layout->addWidget(m_legendCustomFrame);

    return box;
}

void LatexExportDialog::onLegendModeChanged() {
    if (!m_legendPresetFrame) return;
    m_legendPresetFrame->setVisible(m_legendPresetRadio->isChecked());
    m_legendCustomFrame->setVisible(m_legendCustomRadio->isChecked());
}

// -------------------------------------------------------------- curves
namespace {

// Fixed-width label that elides long names ("abc...xyz") so they can't push the rest of
// the row to the right; the complete name is shown as a tooltip on hover.
QLabel *makeElidedNameLabel(const QString &fullText, int width = 190) {
    auto *l = new QLabel;
    l->setFixedWidth(width);
    l->setText(QFontMetrics(l->font()).elidedText(fullText, Qt::ElideMiddle, width));
    l->setToolTip(fullText);
    return l;
}

const char *kCurveRowMime = "application/x-plotter-curve-row";

// Small "grip" the user drags to reorder a curve row.
class CurveDragHandle : public QLabel {
public:
    explicit CurveDragHandle(int rowId) : QLabel(QString::fromUtf8("\u2630")), m_rowId(rowId) {
        setAlignment(Qt::AlignCenter);
        setFixedWidth(22);
        setCursor(Qt::OpenHandCursor);
        setToolTip("Drag up or down to change the plot / legend order");
    }
protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) m_press = e->pos();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!(e->buttons() & Qt::LeftButton)) return;
        if ((e->pos() - m_press).manhattanLength() < QApplication::startDragDistance()) return;
        auto *drag = new QDrag(this);
        auto *mime = new QMimeData;
        mime->setData(kCurveRowMime, QByteArray::number(m_rowId));
        drag->setMimeData(mime);
        drag->exec(Qt::MoveAction);
    }
private:
    int m_rowId;
    QPoint m_press;
};

// Group box that accepts a dragged curve row and reports where it is dropped.
class CurveDropBox : public QGroupBox {
public:
    using QGroupBox::QGroupBox;
    std::function<void(int y)> onHover;
    std::function<void()> onLeave;
    std::function<void(int y, int rowId)> onDrop;
protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasFormat(kCurveRowMime)) e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (!e->mimeData()->hasFormat(kCurveRowMime)) return;
        e->acceptProposedAction();
        if (onHover) onHover(e->position().toPoint().y());
    }
    void dragLeaveEvent(QDragLeaveEvent *e) override {
        if (onLeave) onLeave();
        QGroupBox::dragLeaveEvent(e);
    }
    void dropEvent(QDropEvent *e) override {
        if (!e->mimeData()->hasFormat(kCurveRowMime)) return;
        int id = e->mimeData()->data(kCurveRowMime).toInt();
        e->acceptProposedAction();
        if (onLeave) onLeave();
        if (onDrop) onDrop(e->position().toPoint().y(), id);
    }
};

} // namespace

QGroupBox *LatexExportDialog::buildCurvesSection() {
    auto *dropBox = new CurveDropBox("Curves (color / marker / line / legend)  -  drag the grip to reorder");
    auto *box = dropBox;
    dropBox->setAcceptDrops(true);
    auto *grid = new QGridLayout(box);
    grid->setHorizontalSpacing(8);
    m_curveGrid = grid;

    // thin blue line that shows where the dragged row will land
    m_curveDropLine = new QFrame(box);
    m_curveDropLine->setFixedHeight(3);
    m_curveDropLine->setStyleSheet("background:#3d8bfd;");
    m_curveDropLine->hide();
    dropBox->onHover = [this](int y) { showCurveDropLine(y); };
    dropBox->onLeave = [this]() { m_curveDropLine->hide(); };
    dropBox->onDrop = [this](int y, int rowId) {
        int from = curveRowIndexById(rowId);
        if (from < 0) return;
        int ins = curveInsertIndexAt(y);
        int to = ins > from ? ins - 1 : ins;
        moveCurveRow(from, to);
    };

    const bool lineType = (m_plotType == "line");
    QStringList headers = lineType
        ? QStringList{ "Order", "Column", "Legend label", "Color", "Marker", "Line style", "Line pt" }
        : QStringList{ "Order", "Column", "Legend label", "Fill color", "Fill type", "Fill opacity",
                       "Outline color", "Outline style", "Outline pt" };
    for (int col = 0; col < headers.size(); ++col) {
        auto *l = new QLabel(headers[col]);
        l->setStyleSheet("font-weight:bold;");
        grid->addWidget(l, 0, col, Qt::AlignLeft | Qt::AlignVCenter);
    }

    QString style = m_app->currentStyleText();
    QString defaultMarker = (style == "Line") ? "none" : "*";
    QString defaultLineStyle = (style == "Markers only") ? "none" : "solid";

    int rowIdx = 1;
    int nextId = 0;
    for (const auto &yCol : m_app->currentYCols()) {
        QString label = m_app->curveLabelPublic(yCol);
        const int rowId = nextId++;

        // ---- order controls: grip + up / down buttons ----
        auto *orderWidget = new QWidget;
        auto *orderLayout = new QHBoxLayout(orderWidget);
        orderLayout->setContentsMargins(0, 0, 0, 0);
        orderLayout->setSpacing(1);
        orderLayout->addWidget(new CurveDragHandle(rowId));
        auto *upBtn = new QToolButton;
        upBtn->setText(QString::fromUtf8("\u25B2"));
        upBtn->setAutoRaise(true);
        upBtn->setToolTip("Move up");
        auto *downBtn = new QToolButton;
        downBtn->setText(QString::fromUtf8("\u25BC"));
        downBtn->setAutoRaise(true);
        downBtn->setToolTip("Move down");
        orderLayout->addWidget(upBtn);
        orderLayout->addWidget(downBtn);
        connect(upBtn, &QToolButton::clicked, this, [this, rowId]() {
            int i = curveRowIndexById(rowId);
            if (i > 0) moveCurveRow(i, i - 1);
        });
        connect(downBtn, &QToolButton::clicked, this, [this, rowId]() {
            int i = curveRowIndexById(rowId);
            if (i >= 0 && i + 1 < m_curveRows.size()) moveCurveRow(i, i + 1);
        });
        grid->addWidget(orderWidget, rowIdx, 0, Qt::AlignLeft | Qt::AlignVCenter);

        QString shownLabel = label + (yCol.y2 ? " [Y2]" : "");
        auto *nameLabel = makeElidedNameLabel(shownLabel);
        grid->addWidget(nameLabel, rowIdx, 1, Qt::AlignLeft | Qt::AlignVCenter);

        auto *labelEdit = new QLineEdit(label);
        labelEdit->setMinimumWidth(110);
        grid->addWidget(labelEdit, rowIdx, 2, Qt::AlignVCenter);

        QColor onScreenColor = m_app->curveColor(yCol);
        QString defaultColorName = "blue";
        // match our fixed tab10 plotting cycle back to a LaTeX color name by hex
        for (int i = 0; i < fileColors().size() && i < tab10LatexNames().size(); ++i) {
            if (fileColors()[i] == onScreenColor) { defaultColorName = tab10LatexNames()[i]; break; }
        }
        // The color belongs to the curve (it is picked here, per row), so reordering rows
        // never reassigns colors.
        auto *colorPicker = new ColorPickerWidget(defaultColorName);
        grid->addWidget(colorPicker, rowIdx, 3, Qt::AlignLeft | Qt::AlignVCenter);

        // The marker / line-style combos always exist (the export code reads them) but are
        // only shown for line plots.
        auto *markerCombo = new QComboBox(box);
        markerCombo->addItems(latexMarks());
        markerCombo->setCurrentText(lineType ? defaultMarker : QString("none"));
        markerCombo->setMaximumWidth(130);
        auto *styleCombo = new QComboBox(box);
        styleCombo->addItems(latexLineStyles());
        styleCombo->setCurrentText(lineType ? defaultLineStyle : QString("solid"));
        styleCombo->setMaximumWidth(100);
        auto *widthEdit = new QLineEdit("0.4");
        widthEdit->setMaximumWidth(45);

        CurveRow cr{ yCol, labelEdit, colorPicker, markerCombo, styleCombo, widthEdit, {}, {} };
        if (lineType) {
            grid->addWidget(markerCombo, rowIdx, 4, Qt::AlignLeft | Qt::AlignVCenter);
            grid->addWidget(styleCombo, rowIdx, 5, Qt::AlignLeft | Qt::AlignVCenter);
            grid->addWidget(widthEdit, rowIdx, 6, Qt::AlignLeft | Qt::AlignVCenter);
            cr.cells = { orderWidget, nameLabel, labelEdit, colorPicker, markerCombo, styleCombo, widthEdit };
        } else {
            markerCombo->hide();
            styleCombo->hide();
            auto *opacityEdit = new QLineEdit(m_plotType == "hist" ? "0.55" : "1");
            opacityEdit->setMaximumWidth(45);
            opacityEdit->setToolTip("0 = transparent, 1 = solid (only for a solid fill: patterns ignore it)");

            auto *fillTypeCombo = new QComboBox(box);
            fillTypeCombo->addItems(latexFillTypes());
            fillTypeCombo->setMaximumWidth(140);
            fillTypeCombo->setToolTip("solid: plain fill color\nnone: no fill\n"
                                      "the others are patterns (stripes, dots, ...) drawn in the fill color");
            connect(fillTypeCombo, &QComboBox::currentTextChanged, opacityEdit,
                    [opacityEdit](const QString &t) { opacityEdit->setEnabled(t == "solid"); });

            auto *outlineColorPicker = new ColorPickerWidget("black");
            auto *outlineStyleCombo = new QComboBox(box);
            outlineStyleCombo->addItems(latexLineStyles());
            outlineStyleCombo->setMaximumWidth(100);
            outlineStyleCombo->setToolTip("Line style of the outline ('none' = no outline)");

            widthEdit->setToolTip("Outline width in pt (0 = no outline)");
            cr.opacityEdit = opacityEdit;
            cr.fillTypeCombo = fillTypeCombo;
            cr.outlineColorPicker = outlineColorPicker;
            cr.outlineStyleCombo = outlineStyleCombo;
            cr.cells = { orderWidget, nameLabel, labelEdit, colorPicker, fillTypeCombo, opacityEdit,
                         outlineColorPicker, outlineStyleCombo, widthEdit };
            for (int c = 4; c < cr.cells.size(); ++c)
                grid->addWidget(cr.cells[c], rowIdx, c, Qt::AlignLeft | Qt::AlignVCenter);
        }
        cr.id = rowId;
        m_curveRows.push_back(cr);
        rowIdx++;
    }

    grid->setColumnStretch(headers.size(), 1);
    return box;
}

int LatexExportDialog::curveRowIndexById(int id) const {
    for (int i = 0; i < m_curveRows.size(); ++i)
        if (m_curveRows[i].id == id) return i;
    return -1;
}

// Re-places each row's widgets in the grid according to the current m_curveRows order.
void LatexExportDialog::relayoutCurveRows() {
    for (int i = 0; i < m_curveRows.size(); ++i) {
        const auto &cells = m_curveRows[i].cells;
        for (int c = 0; c < cells.size(); ++c) {
            m_curveGrid->removeWidget(cells[c]);
            Qt::Alignment al = (c == 2) ? Qt::AlignVCenter : (Qt::AlignLeft | Qt::AlignVCenter);
            m_curveGrid->addWidget(cells[c], i + 1, c, al);
        }
    }
}

// Re-places the rows of the fit / integral sections so they follow the curve order.
void LatexExportDialog::relayoutSectionRows(QGridLayout *grid, QVector<SectionRow> &rows, int stretchCol) {
    if (!grid) return;
    std::stable_sort(rows.begin(), rows.end(), [this](const SectionRow &a, const SectionRow &b) {
        return curveRowIndexById(a.curveId) < curveRowIndexById(b.curveId);
    });
    for (int i = 0; i < rows.size(); ++i) {
        const auto &cells = rows[i].cells;
        for (int c = 0; c < cells.size(); ++c) {
            grid->removeWidget(cells[c]);
            Qt::Alignment al = (c == stretchCol) ? Qt::Alignment() : (Qt::AlignLeft | Qt::AlignVCenter);
            grid->addWidget(cells[c], i, c, al);
        }
    }
}

void LatexExportDialog::moveCurveRow(int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= m_curveRows.size() || to >= m_curveRows.size()) return;

    // Remember the colors as they are now. The color *order* is positional: after the move,
    // row 1 gets the color row 1 had before (and so on), so the palette keeps its sequence
    // while the curves change places.
    QVector<QString> positionalColors;
    QMap<int, QString> oldCurveColor;
    for (const auto &row : m_curveRows) {
        positionalColors << row.colorPicker->colorToken();
        oldCurveColor[row.id] = row.colorPicker->colorToken();
    }
    QMap<QPair<int, int>, QString> oldFitColor; // (curveId, fitSlot) -> color
    for (const auto &fr : m_fitRows) oldFitColor[{fr.curveId, fr.fitSlot}] = fr.picker->colorToken();

    CurveRow r = m_curveRows.takeAt(from);
    m_curveRows.insert(to, r);
    relayoutCurveRows();

    for (int i = 0; i < m_curveRows.size(); ++i) m_curveRows[i].colorPicker->setColorToken(positionalColors[i]);

    // Fit / integral colors that simply mirrored their curve's color follow it to the new color
    // (colors the user picked by hand are left alone).
    for (auto &fr : m_fitRows) {
        int ci = curveRowIndexById(fr.curveId);
        if (ci < 0) continue;
        if (fr.picker->colorToken() == oldCurveColor.value(fr.curveId))
            fr.picker->setColorToken(m_curveRows[ci].colorPicker->colorToken());
    }
    for (auto &ir : m_integralRows) {
        int ci = curveRowIndexById(ir.curveId);
        if (ci < 0) continue;
        const CurveRow &cr = m_curveRows[ci];
        QString expectedOld = oldCurveColor.value(ir.curveId);
        QString newDefault = cr.colorPicker->colorToken();
        if (ir.fitSlot >= 0) {
            expectedOld = oldFitColor.value({ir.curveId, ir.fitSlot}, expectedOld);
            auto it = cr.fitColorPickers.find(ir.fitSlot);
            if (it != cr.fitColorPickers.end()) newDefault = it.value()->colorToken();
        }
        if (ir.picker->colorToken() == expectedOld) ir.picker->setColorToken(newDefault);
    }

    relayoutSectionRows(m_fitRowsGrid, m_fitRows, 2);
    relayoutSectionRows(m_integralRowsGrid, m_integralRows, -1);
}

// Index (0..n) at which a row dropped at this y (in curve-box coordinates) would be inserted.
int LatexExportDialog::curveInsertIndexAt(int yInBox) const {
    auto *box = m_curveGrid->parentWidget();
    int idx = 0;
    for (const auto &row : m_curveRows) {
        QWidget *w = row.cells.value(2);
        if (!w) continue;
        int center = w->mapTo(box, QPoint(0, 0)).y() + w->height() / 2;
        if (center < yInBox) ++idx;
    }
    return idx;
}

void LatexExportDialog::showCurveDropLine(int yInBox) {
    if (m_curveRows.isEmpty()) return;
    auto *box = m_curveGrid->parentWidget();
    int ins = curveInsertIndexAt(yInBox);
    int y;
    if (ins < m_curveRows.size()) {
        QWidget *w = m_curveRows[ins].cells.value(2);
        y = w->mapTo(box, QPoint(0, 0)).y() - 3;
    } else {
        QWidget *w = m_curveRows.last().cells.value(2);
        y = w->mapTo(box, QPoint(0, 0)).y() + w->height() + 1;
    }
    m_curveDropLine->setGeometry(8, y, box->width() - 16, 3);
    m_curveDropLine->show();
    m_curveDropLine->raise();
}

// ---------------------------------------------------------------- bars
QGroupBox *LatexExportDialog::buildBarSection() {
    const bool bar = (m_plotType == "bar");
    m_barGroup = new QGroupBox(bar ? "Bar chart" : "Histogram");
    auto *grid = new QGridLayout(m_barGroup);
    int r = 0;

    // bar width only exists for bar charts (a histogram's bars are its bins)
    m_barWidthEdit = new QLineEdit("8");
    m_barWidthEdit->setMaximumWidth(60);
    m_barWidthEdit->setToolTip("Width of one bar. Bars of different series sit side by side, "
                               "so a group is (number of series) x this width.");
    if (bar) {
        grid->addWidget(new QLabel("Bar width (pt):"), r, 0);
        grid->addWidget(m_barWidthEdit, r++, 1, Qt::AlignLeft);
    }

    m_histTickCombo = new QComboBox;
    m_histTickCombo->addItems({"Auto", "Bin edges", "Bin centers"});
    m_histTickCombo->setCurrentIndex(m_app->histTickModePublic());
    m_histTickCombo->setMaximumWidth(130);
    m_histTickCombo->setToolTip("Where the ticks along the bins are placed (X, or Y when sideways): LaTeX's choice, on every bin edge, or in the middle of every bin");
    if (!bar) {
        grid->addWidget(new QLabel("Bin ticks at:"), r, 0);
        grid->addWidget(m_histTickCombo, r++, 1, Qt::AlignLeft);
    }
    grid->setColumnStretch(2, 1);
    m_barGroup->setVisible(m_plotType != "line");
    return m_barGroup;
}

// ---------------------------------------------------------- error bars
QGroupBox *LatexExportDialog::buildErrorBarsSection() {
    m_errGroup = new QGroupBox("Error bars");
    auto *layout = new QVBoxLayout(m_errGroup);

    // Only offered when the main window has error bars on with a std-dev column chosen.
    // Each curve has its own std-dev column; curves without one are exported without bars.
    bool anyCurveHasErr = false;
    for (const auto &k : m_app->currentYCols())
        if (!m_app->curveErrArrayPublic(k).isEmpty()) { anyCurveHasErr = true; break; }
    m_errAvailable = m_app->errorBarsEnabled() && anyCurveHasErr;

    m_includeErrCheck = new QCheckBox(QString::fromUtf8("Include error bars (\u00b1, for the curves that have a std-dev column selected)"));
    m_includeErrCheck->setChecked(true);
    layout->addWidget(m_includeErrCheck);

    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel("Direction:"), 0, 0);
    m_errDirCombo = new QComboBox;
    m_errDirCombo->addItems({"both", "plus", "minus"});
    m_errDirCombo->setMaximumWidth(110);
    grid->addWidget(m_errDirCombo, 0, 1, Qt::AlignLeft);

    grid->addWidget(new QLabel("Color:"), 1, 0);
    m_errSameColorCheck = new QCheckBox("Same as curve");
    m_errSameColorCheck->setChecked(true);
    grid->addWidget(m_errSameColorCheck, 1, 1, Qt::AlignLeft);
    m_errColorPicker = new ColorPickerWidget("black");
    m_errColorPicker->setEnabled(false);
    grid->addWidget(m_errColorPicker, 1, 2, Qt::AlignLeft);
    connect(m_errSameColorCheck, &QCheckBox::toggled, this,
            [this](bool same) { m_errColorPicker->setEnabled(!same); });

    grid->addWidget(new QLabel("Line width (pt):"), 2, 0);
    m_errWidthEdit = new QLineEdit("0.4");
    m_errWidthEdit->setMaximumWidth(60);
    grid->addWidget(m_errWidthEdit, 2, 1, Qt::AlignLeft);

    grid->addWidget(new QLabel("Line style:"), 3, 0);
    m_errStyleCombo = new QComboBox;
    m_errStyleCombo->addItems(latexLineStyles());
    m_errStyleCombo->setMaximumWidth(140);
    grid->addWidget(m_errStyleCombo, 3, 1, Qt::AlignLeft);

    grid->addWidget(new QLabel("End mark:"), 4, 0);
    m_errCapCombo = new QComboBox;
    m_errCapCombo->addItems({"cap", "none", "dot", "diamond", "square"});
    m_errCapCombo->setMaximumWidth(110);
    grid->addWidget(m_errCapCombo, 4, 1, Qt::AlignLeft);

    grid->addWidget(new QLabel("Mark size (pt):"), 5, 0);
    m_errCapSizeEdit = new QLineEdit("2");
    m_errCapSizeEdit->setMaximumWidth(60);
    grid->addWidget(m_errCapSizeEdit, 5, 1, Qt::AlignLeft);
    grid->setColumnStretch(3, 1);
    layout->addLayout(grid);

    if (!m_errAvailable) {
        // Nothing to export: keep the group out of the way.
        m_includeErrCheck->setChecked(false);
        m_errGroup->setVisible(false);
    }
    return m_errGroup;
}

// -------------------------------------------------------------- fit
QGroupBox *LatexExportDialog::buildFitSection() {
    auto *box = new QGroupBox("Curve fit");
    auto *layout = new QVBoxLayout(box);

    m_includeFitCheck = new QCheckBox("Include the currently-shown curve fit(s) as analytic \\addplot");
    m_includeFitCheck->setChecked(m_hasFit);
    m_includeFitCheck->setEnabled(m_hasFit);
    layout->addWidget(m_includeFitCheck);
    if (!m_hasFit) {
        auto *l = new QLabel("(enable & apply a fit in the main window first to include it)");
        l->setStyleSheet("color:#777;");
        layout->addWidget(l);
    }

    auto *sampRow = new QWidget;
    auto *sl = new QHBoxLayout(sampRow);
    sl->setContentsMargins(0, 4, 0, 0);
    sl->addWidget(new QLabel("Samples:"));
    m_fitSamplesEdit = new QLineEdit("200");
    m_fitSamplesEdit->setMaximumWidth(50);
    sl->addWidget(m_fitSamplesEdit);
    sl->addWidget(new QLabel("Line width (pt):"));
    m_fitWidthEdit = new QLineEdit("0.4");
    m_fitWidthEdit->setMaximumWidth(50);
    sl->addWidget(m_fitWidthEdit);
    sl->addWidget(new QLabel("Line style:"));
    m_fitStyleCombo = new QComboBox;
    m_fitStyleCombo->addItems(latexLineStyles());
    m_fitStyleCombo->setCurrentText("dashed");
    m_fitStyleCombo->setMaximumWidth(100);
    sl->addWidget(m_fitStyleCombo);
    sl->addStretch();
    layout->addWidget(sampRow);

    if (m_hasFit) {
        auto *colorBox = new QWidget;
        auto *cl = new QVBoxLayout(colorBox);
        cl->setContentsMargins(0, 6, 0, 0);
        cl->addWidget(new QLabel("Fit color / legend label per curve & interval:"));

        // A shared grid (rather than one QHBoxLayout per row) is used here so
        // that the color picker and label edit columns line up even though
        // curve names/interval suffixes differ in length row to row.
        auto *rowsWidget = new QWidget;
        auto *rowsGrid = new QGridLayout(rowsWidget);
        rowsGrid->setContentsMargins(0, 0, 0, 0);
        rowsGrid->setHorizontalSpacing(8);
        rowsGrid->setVerticalSpacing(3);
        m_fitRowsGrid = rowsGrid;
        int gridRow = 0;
        for (auto &row : m_curveRows) {
            QVector<int> slotIdxs;
            for (auto it = m_fitResults.constBegin(); it != m_fitResults.constEnd(); ++it)
                if (it.key().first == row.yCol) slotIdxs << it.key().second;
            if (slotIdxs.isEmpty()) continue;
            std::sort(slotIdxs.begin(), slotIdxs.end());
            bool multiple = slotIdxs.size() > 1;
            int n = 1;
            for (int idx : slotIdxs) {
                QString suffix = multiple ? QString(" (fit %1)").arg(n) : " (fit)";
                auto *nameLabel = makeElidedNameLabel(m_app->curveLabelPublic(row.yCol) + (row.yCol.y2 ? " [Y2]" : "") + suffix);
                rowsGrid->addWidget(nameLabel, gridRow, 0, Qt::AlignLeft | Qt::AlignVCenter);
                auto *fitColorPicker = new ColorPickerWidget(row.colorPicker->colorToken());
                rowsGrid->addWidget(fitColorPicker, gridRow, 1, Qt::AlignLeft | Qt::AlignVCenter);
                QString curveLabelTxt = row.labelEdit->text().trimmed();
                if (curveLabelTxt.isEmpty()) curveLabelTxt = m_app->curveLabelPublic(row.yCol);
                auto *fitLabelEdit = new QLineEdit(curveLabelTxt + suffix);
                fitLabelEdit->setMinimumWidth(120);
                rowsGrid->addWidget(fitLabelEdit, gridRow, 2);
                row.fitColorPickers[idx] = fitColorPicker;
                row.fitLabelEdits[idx] = fitLabelEdit;
                m_fitRows.push_back({ row.id, idx, { nameLabel, fitColorPicker, fitLabelEdit }, fitColorPicker });
                gridRow++;
                n++;
            }
        }
        rowsGrid->setColumnStretch(3, 1);
        cl->addWidget(rowsWidget);
        layout->addWidget(colorBox);
    }

    return box;
}

// -------------------------------------------------------------- integral
QGroupBox *LatexExportDialog::buildIntegralSection() {
    auto *box = new QGroupBox("Integral / area under curve");
    auto *layout = new QVBoxLayout(box);

    m_includeIntegralCheck = new QCheckBox("Include the currently-shown integral region(s) as shaded \\addplot");
    m_includeIntegralCheck->setChecked(m_hasIntegral);
    m_includeIntegralCheck->setEnabled(m_hasIntegral);
    layout->addWidget(m_includeIntegralCheck);
    if (!m_hasIntegral) {
        auto *l = new QLabel("(enable & place the integral range in the main window first)");
        l->setStyleSheet("color:#777;");
        layout->addWidget(l);
    }

    auto *opacRow = new QWidget;
    auto *ol = new QHBoxLayout(opacRow);
    ol->setContentsMargins(0, 4, 0, 0);
    ol->addWidget(new QLabel("Fill opacity (0-1):"));
    m_integralOpacityEdit = new QLineEdit("0.3");
    m_integralOpacityEdit->setMaximumWidth(50);
    ol->addWidget(m_integralOpacityEdit);
    ol->addStretch();
    layout->addWidget(opacRow);

    if (m_hasIntegral) {
        auto *colorBox = new QWidget;
        auto *cl = new QVBoxLayout(colorBox);
        cl->setContentsMargins(0, 6, 0, 0);
        cl->addWidget(new QLabel("Fill color per region:"));

        // As with the fit section above, a shared grid keeps the color-picker
        // column aligned even though region labels (curve name + fit/interval
        // suffixes) vary in length from row to row.
        auto *rowsWidget = new QWidget;
        auto *rowsGrid = new QGridLayout(rowsWidget);
        rowsGrid->setContentsMargins(0, 0, 0, 0);
        rowsGrid->setHorizontalSpacing(8);
        rowsGrid->setVerticalSpacing(3);
        m_integralRowsGrid = rowsGrid;
        int gridRow = 0;
        for (auto &row : m_curveRows) {
            for (const QString &source : distinctIntegralSources(row.yCol, m_integralResults)) {
                int fitSlot = fitSlotFromSource(source);
                QVector<int> slotIdxs;
                for (auto it = m_integralResults.constBegin(); it != m_integralResults.constEnd(); ++it) {
                    const auto &k = it.key(); // ((yCol, source), slotIdx)
                    if (k.first.first == row.yCol && k.first.second == source) slotIdxs << k.second;
                }
                if (slotIdxs.isEmpty()) continue;
                std::sort(slotIdxs.begin(), slotIdxs.end());
                bool multiple = slotIdxs.size() > 1;
                int n = 1;
                for (int idx : slotIdxs) {
                    QString labelTxt = m_app->curveLabelPublic(row.yCol);
                    if (fitSlot >= 0) labelTxt += QString(" (fit %1)").arg(fitSlot + 1);
                    if (multiple) labelTxt += QString(" [interval %1]").arg(n);
                    auto *nameLabel = makeElidedNameLabel(labelTxt + (row.yCol.y2 ? " [Y2]" : ""));
                    rowsGrid->addWidget(nameLabel, gridRow, 0, Qt::AlignLeft | Qt::AlignVCenter);

                    QString defaultColor = row.colorPicker->colorToken();
                    if (fitSlot >= 0) {
                        auto it2 = row.fitColorPickers.find(fitSlot);
                        if (it2 != row.fitColorPickers.end()) defaultColor = it2.value()->colorToken();
                    }
                    auto *colorPicker = new ColorPickerWidget(defaultColor);
                    rowsGrid->addWidget(colorPicker, gridRow, 1, Qt::AlignLeft | Qt::AlignVCenter);

                    IntegralResultKey key(qMakePair(row.yCol, source), idx);
                    m_integralColorPickers[key] = colorPicker;
                    m_integralRows.push_back({ row.id, fitSlot, { nameLabel, colorPicker }, colorPicker });
                    gridRow++;
                    n++;
                }
            }
        }
        rowsGrid->setColumnStretch(2, 1);
        cl->addWidget(rowsWidget);
        layout->addWidget(colorBox);
    }

    m_integralShowLabelCheck = new QCheckBox(QString::fromUtf8("Label the region with its numeric area (A \u2248 \u2026)"));
    m_integralShowLabelCheck->setChecked(true);
    layout->addWidget(m_integralShowLabelCheck);

    return box;
}

// -------------------------------------------------------------- extra fn
QGroupBox *LatexExportDialog::buildExtraFunctionSection() {
    auto *box = new QGroupBox("Extra function series (optional, e.g. a theoretical curve)");
    auto *grid = new QGridLayout(box);

    grid->addWidget(new QLabel("f(x) ="), 0, 0);
    m_extraExprEdit = new QLineEdit;
    grid->addWidget(m_extraExprEdit, 0, 1, 1, 3);
    auto *hint = new QLabel("pgfmath syntax, e.g. 2*x + 1  or  sin(deg(x))");
    hint->setStyleSheet("color:#555;");
    grid->addWidget(hint, 1, 0, 1, 4);

    double xlo, xhi;
    m_app->currentXRange(xlo, xhi);
    grid->addWidget(new QLabel("Domain min:"), 2, 0);
    m_extraDomainMinEdit = new QLineEdit(QString::number(xlo, 'g', 6));
    grid->addWidget(m_extraDomainMinEdit, 2, 1);
    grid->addWidget(new QLabel("Domain max:"), 2, 2);
    m_extraDomainMaxEdit = new QLineEdit(QString::number(xhi, 'g', 6));
    grid->addWidget(m_extraDomainMaxEdit, 2, 3);

    grid->addWidget(new QLabel("Color:"), 3, 0);
    m_extraColorPicker = new ColorPickerWidget("black");
    grid->addWidget(m_extraColorPicker, 3, 1);
    grid->addWidget(new QLabel("Legend label:"), 3, 2);
    m_extraLabelEdit = new QLineEdit;
    grid->addWidget(m_extraLabelEdit, 3, 3);

    grid->addWidget(new QLabel("Samples:"), 4, 0);
    m_extraSamplesEdit = new QLineEdit("200");
    grid->addWidget(m_extraSamplesEdit, 4, 1);
    grid->addWidget(new QLabel("Line width (pt):"), 4, 2);
    m_extraWidthEdit = new QLineEdit("0.4");
    grid->addWidget(m_extraWidthEdit, 4, 3);

    return box;
}

// -------------------------------------------------------------- figure
QGroupBox *LatexExportDialog::buildFigureSection() {
    auto *box = new QGroupBox("Figure wrapper (optional)");
    auto *grid = new QGridLayout(box);

    m_wrapFigureCheck = new QCheckBox(QString::fromUtf8("Wrap the plot in a \\begin{figure} ... \\end{figure} environment"));
    grid->addWidget(m_wrapFigureCheck, 0, 0, 1, 4);
    connect(m_wrapFigureCheck, &QCheckBox::toggled, this, &LatexExportDialog::onWrapFigureChanged);

    grid->addWidget(new QLabel("Caption:"), 1, 0);
    m_captionEdit = new QLineEdit;
    grid->addWidget(m_captionEdit, 1, 1, 1, 3);

    grid->addWidget(new QLabel("Label:"), 2, 0);
    m_figLabelEdit = new QLineEdit("fig:");
    grid->addWidget(m_figLabelEdit, 2, 1);

    grid->addWidget(new QLabel("Placement:"), 2, 2);
    m_placementCombo = new QComboBox;
    m_placementCombo->addItems({"htbp", "ht", "h", "t", "b", "p", "H"});
    m_placementCombo->setMaximumWidth(90);
    grid->addWidget(m_placementCombo, 2, 3);

    return box;
}

void LatexExportDialog::onWrapFigureChanged() {
    bool on = m_wrapFigureCheck->isChecked();
    m_captionEdit->setEnabled(on);
    m_figLabelEdit->setEnabled(on);
    m_placementCombo->setEnabled(on);
}

// ----------------------------------------------------------- collectOpts
LatexExportOptions LatexExportDialog::collectOpts() {
    LatexExportOptions opts;

    bool hasXmin, hasXmax, hasYmin, hasYmax;
    double xmin = parseOptionalDouble(m_xminEdit, "X min", hasXmin);
    double xmax = parseOptionalDouble(m_xmaxEdit, "X max", hasXmax);
    double ymin = parseOptionalDouble(m_yminEdit, "Y min", hasYmin);
    double ymax = parseOptionalDouble(m_ymaxEdit, "Y max", hasYmax);
    opts.hasXlim = hasXmin && hasXmax;
    if (opts.hasXlim) { opts.xmin = xmin; opts.xmax = xmax; }
    opts.hasYlim = hasYmin && hasYmax;
    if (opts.hasYlim) { opts.ymin = ymin; opts.ymax = ymax; }

    const bool numAsText = (m_plotType == "bar") && m_app->barNumbersAsText();
    bool embedding = m_sourceEmbedRadio->isChecked() || m_plotType == "hist" || numAsText;
    opts.dataSource = embedding ? "embed" : "csv";
    opts.plotType = m_plotType;
    const bool flipped = (m_plotType == "bar" || m_plotType == "hist") && m_app->barsFlipped();
    opts.flipBars = flipped;
    {
        bool hasRotX = false, hasRotY = false;
        opts.xTickLabelRotate = parseOptionalDouble(m_xLabelRotEdit, "X tick label rotation", hasRotX);
        opts.hasXTickLabelRotate = hasRotX;
        opts.xTickLabelAnchor = m_xLabelAnchorCombo->currentText();
        opts.yTickLabelRotate = parseOptionalDouble(m_yLabelRotEdit, "Y tick label rotation", hasRotY);
        opts.hasYTickLabelRotate = hasRotY;
        opts.yTickLabelAnchor = m_yLabelAnchorCombo->currentText();
        if (m_y2LabelRotEdit) {
            bool hasRotY2 = false;
            opts.y2TickLabelRotate = parseOptionalDouble(m_y2LabelRotEdit, "Y2 tick label rotation", hasRotY2);
            opts.hasY2TickLabelRotate = hasRotY2;
            opts.y2TickLabelAnchor = m_y2LabelAnchorCombo->currentText();
        }
    }
    if (m_plotType == "bar") {
        bool okBw = false;
        opts.barWidthPt = m_barWidthEdit->text().trimmed().toDouble(&okBw);
        if (!okBw || opts.barWidthPt <= 0) throw std::runtime_error("bar width must be a positive number");
    }

    // ---- second Y axis ----
    opts.hasSecondAxis = m_hasSecondAxis;
    if (opts.hasSecondAxis) {
        opts.y2label = m_y2labelEdit->text().trimmed();
        bool hasY2min, hasY2max, hasY2tick;
        double y2min = parseOptionalDouble(m_y2minEdit, "Y2 min", hasY2min);
        double y2max = parseOptionalDouble(m_y2maxEdit, "Y2 max", hasY2max);
        opts.hasY2lim = hasY2min && hasY2max;
        if (opts.hasY2lim) { opts.y2min = y2min; opts.y2max = y2max; }
        opts.logy2 = m_logy2Check->isChecked();
        opts.y2tickStep = parseOptionalDouble(m_y2tickEdit, "Y2 tick step", hasY2tick);
        opts.hasY2tickStep = hasY2tick;
        opts.y2decimals = m_y2decSpin->value();
        opts.y2DefaultDecimals = m_y2decDefaultCheck->isChecked();
        bool hasRot = false;
        opts.y2labelRotate = parseOptionalDouble(m_y2labelRotateEdit, "Y2 label rotation", hasRot);
        opts.hasY2labelRotate = hasRot;
    }

    for (auto &row : m_curveRows) {
        const CurveKey &yCol = row.yCol;
        QString label = m_app->curveLabelPublic(yCol);
        QString colorName = row.colorPicker->colorToken().trimmed();
        if (colorName.isEmpty()) colorName = "blue";
        bool ok = false;
        double linewidthPt = row.widthEdit->text().trimmed().toDouble(&ok);
        if (!ok) throw std::runtime_error(("line width for '" + label + "' must be a number").toStdString());

        QVector<double> xFull = m_app->curveXArrayPublic(yCol);
        QVector<double> yFull = m_app->curveYArrayPublic(yCol);
        int n = std::min(xFull.size(), yFull.size());

        QVector<double> eFull = m_app->curveErrArrayPublic(yCol);
        bool useErr = m_includeErrCheck->isChecked() && m_errAvailable && !eFull.isEmpty();

        LatexCurveOpts c;
        c.yCol = yCol;
        c.dsIdx = yCol.dsIdx;
        c.hasErr = useErr;
        if (useErr) c.errColName = m_app->errorColumnNamePublic(yCol);
        const Dataset &ds = m_app->datasets()[yCol.dsIdx];
        // a text X column (bar categories) is exported as positions 0, 1, 2, ... like the row index
        const bool textX = !m_app->categoryLabelsPublic(yCol).isEmpty();
        c.xColName = (ds.xColumn == indexLabel() || textX) ? "index" : ds.xColumn;
        // Headerless files have auto-generated names (Col1, Col2, ...) and a row-index X
        // has no real column at all, so reference those by position in the CSV.
        c.xByIndex = (ds.xColumn == indexLabel()) || textX || !ds.info.hasHeader;
        c.yByIndex = !ds.info.hasHeader;
        // The field is pre-filled with the column name; if the user clears it, the legend
        // entry is exported empty (no fallback to the column name).
        c.legendLabel = row.labelEdit->text().trimmed();
        c.color = colorName;
        c.marker = row.markerCombo->currentText();
        c.linestyle = row.lineStyleCombo->currentText();
        c.linewidthPt = linewidthPt;
        if (row.opacityEdit) {
            bool okOp = false;
            double op = row.opacityEdit->text().trimmed().toDouble(&okOp);
            if (!okOp || op < 0 || op > 1)
                throw std::runtime_error(("fill opacity for '" + label + "' must be a number between 0 and 1").toStdString());
            c.fillOpacity = op;
        }
        if (row.fillTypeCombo) {
            c.fillType = row.fillTypeCombo->currentText();
            c.outlineColor = row.outlineColorPicker->colorToken().trimmed();
            if (c.outlineColor.isEmpty()) c.outlineColor = "black";
            c.outlineStyle = row.outlineStyleCombo->currentText();
        }

        if (m_plotType == "hist") {
            QVector<double> edges, heights;
            if (!m_app->histogramPublic(yCol, edges, heights) || heights.isEmpty())
                throw std::runtime_error(("no histogram data for '" + label + "' -- press Plot first").toStdString());
            for (int i = 0; i < heights.size(); ++i) c.points << qMakePair(edges[i], heights[i]);
            c.points << qMakePair(edges.last(), heights.last());   // closes the last bin
        } else if (embedding) {
            QVector<QPair<double, double>> valid;
            QVector<double> validErr;
            QVector<bool> inRange;
            for (int i = 0; i < n; ++i) {
                if (std::isnan(xFull[i]) || std::isnan(yFull[i])) continue;
                valid << qMakePair(xFull[i], yFull[i]);
                if (useErr) {
                    double e = i < eFull.size() ? eFull[i] : std::numeric_limits<double>::quiet_NaN();
                    validErr << (std::isnan(e) ? 0.0 : std::fabs(e));
                }
                // the category / X values run along y when the bars are sideways
                const bool hasCatLim = flipped ? opts.hasYlim : opts.hasXlim;
                const double cmin = flipped ? opts.ymin : opts.xmin, cmax = flipped ? opts.ymax : opts.xmax;
                bool ok = !hasCatLim || (xFull[i] >= cmin && xFull[i] <= cmax);
                inRange << ok;
            }
            // Keep every in-range point, plus the single adjacent out-of-range
            // point on either side of a gap so the drawn line reaches the
            // plot edge instead of stopping short of it. The Y axis limits
            // never affect which points are kept here.
            for (int j = 0; j < valid.size(); ++j) {
                bool keep = inRange[j];
                if (m_plotType == "line") {
                    if (!keep && j > 0 && inRange[j - 1]) keep = true;
                    if (!keep && j + 1 < valid.size() && inRange[j + 1]) keep = true;
                }
                if (keep) {
                    c.points << valid[j];
                    if (useErr) c.errs << validErr[j];
                }
            }
            if (c.points.isEmpty())
                throw std::runtime_error(("'" + label + "' has no valid numeric (x, y) points to export within the exported X range").toStdString());
        } else {
            auto it = m_csvDsRows.find(yCol.dsIdx);
            if (it == m_csvDsRows.end())
                throw std::runtime_error(("no CSV export settings found for '" + label + "'").toStdString());
            c.csvFilename = it.value().filenameEdit->text().trimmed();
            c.csvColsep = it.value().sepCombo->currentText();
            if (c.csvFilename.isEmpty())
                throw std::runtime_error(("enter a CSV filename for '" + ds.basename + "'").toStdString());
        }
        opts.curves << c;
    }

    if (m_plotType == "bar") {
        // text X column: one tick label per bar position that actually has data
        QMap<int, QString> posLabel;
        for (const auto &row : m_curveRows) {
            QStringList cats = m_app->categoryLabelsPublic(row.yCol);
            if (cats.isEmpty()) continue;
            QVector<double> xf = m_app->curveXArrayPublic(row.yCol), yf = m_app->curveYArrayPublic(row.yCol);
            for (int i = 0; i < std::min(xf.size(), yf.size()); ++i) {
                if (std::isnan(xf[i]) || std::isnan(yf[i])) continue;
                int p = int(std::lround(xf[i]));
                if (p >= 0 && p < cats.size()) posLabel[p] = cats[p];
            }
        }
        for (auto it = posLabel.constBegin(); it != posLabel.constEnd(); ++it) {
            opts.xTickPositions << QString::number(it.key());
            opts.xTickLabels << it.value();
        }
    }

    if (m_plotType == "hist" && !opts.curves.isEmpty() && m_histTickCombo->currentIndex() != 0) {
        // ticks on every bin edge / in every bin centre (all series share the same bins)
        QVector<double> edges, heights;
        if (m_app->histogramPublic(m_curveRows.first().yCol, edges, heights) && edges.size() > 1) {
            const bool centres = (m_histTickCombo->currentIndex() == 2);
            if (centres) for (int i = 0; i + 1 < edges.size(); ++i) opts.xTickPositions << QString::number(0.5 * (edges[i] + edges[i + 1]), 'g', 8);
            else for (double e : edges) opts.xTickPositions << QString::number(e, 'g', 8);
        }
    }

    for (const auto &cv : opts.curves) if (cv.hasErr) { opts.errorBars.enabled = true; break; }
    if (opts.errorBars.enabled) {
        bool okW = false, okS = false;
        double ebW = m_errWidthEdit->text().trimmed().toDouble(&okW);
        double ebS = m_errCapSizeEdit->text().trimmed().toDouble(&okS);
        if (!okW) throw std::runtime_error("error bar line width must be a number");
        if (!okS) throw std::runtime_error("error bar mark size must be a number");
        opts.errorBars.direction = m_errDirCombo->currentText();
        opts.errorBars.color = m_errSameColorCheck->isChecked() ? QString() : m_errColorPicker->colorToken().trimmed();
        opts.errorBars.linewidthPt = ebW;
        opts.errorBars.linestyle = m_errStyleCombo->currentText();
        opts.errorBars.capMark = m_errCapCombo->currentText();
        opts.errorBars.capSizePt = ebS;
    }

    // Two overlaid axes only line up if both use the same X range, so make sure one is set
    // (otherwise each axis would autoscale X from its own curves).
    if (opts.hasSecondAxis && m_plotType == "hist" && !(flipped ? opts.hasYlim : opts.hasXlim)) {
        // histogram: both axes share the bin range (the X column plays no part)
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const auto &row : m_curveRows) {
            QVector<double> edges, heights;
            if (!m_app->histogramPublic(row.yCol, edges, heights) || edges.isEmpty()) continue;
            lo = std::min(lo, edges.first()); hi = std::max(hi, edges.last());
        }
        if (lo < hi) {
            if (flipped) { opts.hasYlim = true; opts.ymin = lo; opts.ymax = hi; }
            else { opts.hasXlim = true; opts.xmin = lo; opts.xmax = hi; }
        }
    } else if (opts.hasSecondAxis && flipped && !opts.hasYlim) {
        // sideways bars: the two value axes share the category (vertical) range
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const auto &row : m_curveRows)
            for (double x : m_app->curveXArrayPublic(row.yCol)) {
                if (std::isnan(x)) continue;
                lo = std::min(lo, x); hi = std::max(hi, x);
            }
        if (lo <= hi) { opts.hasYlim = true; opts.ymin = lo - 0.5; opts.ymax = hi + 0.5; }
    }
    if (opts.hasSecondAxis && !opts.hasXlim && !flipped && m_plotType != "hist") {
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (const auto &row : m_curveRows) {
            for (double x : m_app->curveXArrayPublic(row.yCol)) {
                if (std::isnan(x) || (m_logxCheck->isChecked() && x <= 0)) continue;
                lo = std::min(lo, x); hi = std::max(hi, x);
            }
        }
        if (lo < hi) { opts.hasXlim = true; opts.xmin = lo; opts.xmax = hi; }
    }

    opts.includeFit = m_includeFitCheck->isChecked() && m_hasFit;
    if (opts.includeFit) {
        int samples = std::max(2, parseIntOr(m_fitSamplesEdit, 200));
        bool ok = false;
        double fitLinewidth = m_fitWidthEdit->text().trimmed().toDouble(&ok);
        if (!ok) throw std::runtime_error("curve fit line width must be a number");

        for (auto &row : m_curveRows) {
            QVector<int> slotIdxs;
            for (auto it = m_fitResults.constBegin(); it != m_fitResults.constEnd(); ++it)
                if (it.key().first == row.yCol) slotIdxs << it.key().second;
            std::sort(slotIdxs.begin(), slotIdxs.end());
            bool multiple = slotIdxs.size() > 1;
            int n = 1;
            for (int idx : slotIdxs) {
                FitResultKey key(row.yCol, idx);
                if (!m_fitResults.contains(key)) { n++; continue; }
                const FitResult &result = m_fitResults[key];

                QString colorName = "blue";
                auto cIt = row.fitColorPickers.find(idx);
                if (cIt != row.fitColorPickers.end()) colorName = cIt.value()->colorToken();
                if (colorName.trimmed().isEmpty()) colorName = row.colorPicker->colorToken();

                QString suffix = multiple ? QString(" (fit %1)").arg(n) : " (fit)";
                QString fitLabel;
                auto lIt = row.fitLabelEdits.find(idx);
                if (lIt != row.fitLabelEdits.end()) fitLabel = lIt.value()->text().trimmed();
                if (fitLabel.isEmpty() && lIt == row.fitLabelEdits.end()) {
                    QString baseLabel = row.labelEdit->text().trimmed();
                    if (baseLabel.isEmpty()) baseLabel = m_app->curveLabelPublic(row.yCol);
                    fitLabel = baseLabel + suffix;
                }

                LatexFitCurveOpts fc;
                fc.yCol = row.yCol;
                fc.slotIdx = idx;
                fc.domainMin = result.plotXmin;
                fc.domainMax = result.plotXmax;
                fc.color = colorName;
                fc.linestyle = m_fitStyleCombo->currentText();
                fc.legendLabel = fitLabel;
                fc.samples = samples;
                fc.linewidthPt = fitLinewidth;
                fc.wasCustomFit = (result.kind == FitKind::Custom);

                switch (result.kind) {
                case FitKind::Linear:
                    fc.expression = QString("%1*x + (%2)").arg(QString::number(result.params["m"], 'g', 10), QString::number(result.params["b"], 'g', 10));
                    break;
                case FitKind::Polynomial: {
                    QStringList terms;
                    int nn = result.polyCoeffs.size();
                    for (int i = 0; i < nn; ++i) {
                        int p = nn - 1 - i;
                        double c = result.polyCoeffs[i];
                        if (p == 0) terms << QString("(%1)").arg(QString::number(c, 'g', 10));
                        else if (p == 1) terms << QString("(%1)*x").arg(QString::number(c, 'g', 10));
                        else terms << QString("(%1)*x^%2").arg(QString::number(c, 'g', 10)).arg(p);
                    }
                    fc.expression = terms.join(" + ");
                    break;
                }
                case FitKind::Exponential:
                    fc.expression = QString("(%1)*exp(%2*x) + (%3)")
                        .arg(QString::number(result.params["a"], 'g', 10), QString::number(result.params["b"], 'g', 10), QString::number(result.params["c"], 'g', 10));
                    break;
                case FitKind::Logarithmic:
                    fc.expression = QString("(%1)*ln(x) + (%2)")
                        .arg(QString::number(result.params["a"], 'g', 10), QString::number(result.params["b"], 'g', 10));
                    break;
                case FitKind::Power:
                    fc.expression = QString("(%1)*x^(%2)")
                        .arg(QString::number(result.params["a"], 'g', 10), QString::number(result.params["b"], 'g', 10));
                    break;
                case FitKind::Custom:
                    fc.expression = translateCustomExpr(result.customExpr, result.customParamNames, result.customParamValues);
                    break;
                }
                opts.fitCurves << fc;
                n++;
            }
        }
    }

    opts.integralRegions.clear();
    if (m_includeIntegralCheck->isChecked() && m_hasIntegral) {
        bool ok = false;
        double opacity = m_integralOpacityEdit->text().trimmed().toDouble(&ok);
        if (!ok) throw std::runtime_error("integral fill opacity must be a number");
        bool showLabel = m_integralShowLabelCheck->isChecked();

        for (auto &row : m_curveRows) {
            for (const QString &source : distinctIntegralSources(row.yCol, m_integralResults)) {
                int fitSlot = fitSlotFromSource(source);
                QVector<int> slotIdxs;
                for (auto it = m_integralResults.constBegin(); it != m_integralResults.constEnd(); ++it) {
                    const auto &k = it.key();
                    if (k.first.first == row.yCol && k.first.second == source) slotIdxs << k.second;
                }
                std::sort(slotIdxs.begin(), slotIdxs.end());
                bool multiple = slotIdxs.size() > 1;
                int n = 1;
                for (int idx : slotIdxs) {
                    IntegralResultKey key(qMakePair(row.yCol, source), idx);
                    if (!m_integralResults.contains(key)) { n++; continue; }
                    const IntegralResultData &result = m_integralResults[key];
                    if (result.xs.size() < 2) { n++; continue; }

                    QString colorName = "blue";
                    auto cIt = m_integralColorPickers.find(key);
                    if (cIt != m_integralColorPickers.end()) colorName = cIt.value()->colorToken();

                    QVector<QPair<double, double>> coords;
                    for (int i = 0; i < result.xs.size(); ++i) coords << qMakePair(result.xs[i], result.ys[i]);
                    coords << qMakePair(result.xs.last(), 0.0);
                    coords << qMakePair(result.xs.first(), 0.0);

                    QString regionLabel = m_app->curveLabelPublic(row.yCol);
                    if (fitSlot >= 0) regionLabel += QString(" (fit %1)").arg(fitSlot + 1);
                    if (multiple) regionLabel += QString(" [interval %1]").arg(n);

                    double meanX = 0, meanY = 0;
                    for (double x : result.xs) meanX += x;
                    for (double y : result.ys) meanY += y;
                    meanX /= result.xs.size();
                    meanY /= result.ys.size();

                    LatexIntegralRegionOpts reg;
                    reg.regionLabel = regionLabel;
                    reg.y2 = row.yCol.y2;
                    reg.color = colorName;
                    reg.opacity = opacity;
                    reg.coords = coords;
                    reg.area = result.area;
                    reg.showLabel = showLabel;
                    reg.labelX = meanX;
                    reg.labelY = meanY * 0.5;
                    opts.integralRegions << reg;
                    n++;
                }
            }
        }
    }

    if (!embedding) {
        for (auto it = m_csvDsRows.constBegin(); it != m_csvDsRows.constEnd(); ++it) {
            QString fname = it.value().filenameEdit->text().trimmed();
            if (fname.isEmpty())
                throw std::runtime_error(("enter a CSV filename for '" + m_app->datasets()[it.key()].basename + "'").toStdString());
            opts.csvExports[it.key()] = { fname, it.value().sepCombo->currentText() };
        }
        QMap<QString, int> seenNames;
        for (auto it = opts.csvExports.constBegin(); it != opts.csvExports.constEnd(); ++it) {
            if (seenNames.contains(it.value().filename)) {
                int prior = seenNames[it.value().filename];
                throw std::runtime_error((
                    "'" + m_app->datasets()[prior].basename + "' and '" + m_app->datasets()[it.key()].basename +
                    "' are both set to export to the same filename (" + it.value().filename + ") -- give them different filenames"
                ).toStdString());
            }
            seenNames[it.value().filename] = it.key();
        }
    }

    QString extraExpr = m_extraExprEdit->text().trimmed();
    opts.hasExtraFunction = !extraExpr.isEmpty();
    if (opts.hasExtraFunction) {
        bool hasMin, hasMax;
        double dmin = parseOptionalDouble(m_extraDomainMinEdit, "extra function domain min", hasMin);
        double dmax = parseOptionalDouble(m_extraDomainMaxEdit, "extra function domain max", hasMax);
        if (!hasMin || !hasMax) throw std::runtime_error("extra function needs a domain min and max");
        int samples = std::max(2, parseIntOr(m_extraSamplesEdit, 200));
        bool ok = false;
        double extraLinewidth = m_extraWidthEdit->text().trimmed().toDouble(&ok);
        if (!ok) throw std::runtime_error("extra function line width must be a number");

        opts.extraFunction.expression = extraExpr;
        opts.extraFunction.domainMin = dmin;
        opts.extraFunction.domainMax = dmax;
        QString extraColor = m_extraColorPicker->colorToken().trimmed();
        opts.extraFunction.color = extraColor.isEmpty() ? "black" : extraColor;
        opts.extraFunction.legendLabel = m_extraLabelEdit->text().trimmed();
        opts.extraFunction.samples = samples;
        opts.extraFunction.linewidthPt = extraLinewidth;
    }

    bool hasRotate;
    double rotate = parseOptionalDouble(m_ylabelRotateEdit, "Y label rotation", hasRotate);
    opts.hasYlabelRotate = hasRotate;
    if (hasRotate) opts.ylabelRotate = rotate;

    opts.escapeText = m_escapeTextCheck->isChecked();
    opts.title = m_titleEdit->text().trimmed();
    opts.xlabel = m_xlabelEdit->text().trimmed();
    opts.ylabel = m_ylabelEdit->text().trimmed();
    opts.logx = m_logxCheck->isChecked();
    opts.logy = m_logyCheck->isChecked();

    bool hasXtick, hasYtick;
    opts.xtickStep = parseOptionalDouble(m_xtickEdit, "X tick step", hasXtick);
    opts.hasXtickStep = hasXtick;
    opts.ytickStep = parseOptionalDouble(m_ytickEdit, "Y tick step", hasYtick);
    opts.hasYtickStep = hasYtick;
    opts.xdecimals = m_xdecSpin->value();
    opts.ydecimals = m_ydecSpin->value();
    opts.xDefaultDecimals = m_xdecDefaultCheck->isChecked();
    opts.yDefaultDecimals = m_ydecDefaultCheck->isChecked();
    if (m_minorSameCheck->isChecked()) {
        opts.xMinorTicks = opts.yMinorTicks = opts.y2MinorTicks = m_minorCheck->isChecked();
        opts.xMinorTickNum = opts.yMinorTickNum = opts.y2MinorTickNum = m_minorNumCombo->value();
    } else {
        opts.xMinorTicks = m_xMinorCheck->isChecked();
        opts.xMinorTickNum = m_xMinorNum->value();
        opts.yMinorTicks = m_yMinorCheck->isChecked();
        opts.yMinorTickNum = m_yMinorNum->value();
        opts.y2MinorTicks = m_y2MinorCheck && m_y2MinorCheck->isChecked();
        opts.y2MinorTickNum = m_y2MinorNum ? m_y2MinorNum->value() : 3;
    }
    opts.tickAlign = m_tickAlignCombo->currentText();
    opts.xtickPos = m_xtickPosCombo->currentText();
    opts.ytickPos = m_ytickPosCombo->currentText();
    // zero-fill only has an effect on an axis with an explicit decimal count
    opts.zeroFill = m_zeroFillCheck->isChecked()
                    && !(opts.xDefaultDecimals && opts.yDefaultDecimals
                         && (!opts.hasSecondAxis || opts.y2DefaultDecimals));
    opts.hideTicks = m_hideTicksCheck->isChecked();

    opts.gridMode = m_gridModeCombo->currentText();
    opts.axisBox = m_axisBoxCheck->isChecked();

    if (m_legendNoneRadio->isChecked()) opts.legendMode = "none";
    else if (m_legendCustomRadio->isChecked()) opts.legendMode = "custom";
    else opts.legendMode = "preset";
    opts.legendPos = m_legendPosCombo->currentText();
    bool okX, okY;
    double lx = m_legendXEdit->text().trimmed().toDouble(&okX);
    double ly = m_legendYEdit->text().trimmed().toDouble(&okY);
    opts.legendX = okX ? lx : 0.5;
    opts.legendY = okY ? ly : 0.0;
    opts.legendAnchor = m_legendAnchorCombo->currentText();

    bool okW, okH;
    double w = m_widthEdit->text().trimmed().toDouble(&okW);
    double h = m_heightEdit->text().trimmed().toDouble(&okH);
    opts.widthCm = okW ? w : 10;
    opts.heightCm = okH ? h : 7;
    opts.smoothing = m_smoothingCombo->currentText();

    opts.wrapFigure = m_wrapFigureCheck->isChecked();
    opts.caption = m_captionEdit->text().trimmed();
    opts.figLabel = m_figLabelEdit->text().trimmed();
    opts.figurePlacement = m_placementCombo->currentText();

    return opts;
}

// ------------------------------------------- zero-fill / saved settings
// "Same on all axes" greys out the per-axis minor-tick options (and the other way round);
// a spin box is only live while its own check box is ticked.
void LatexExportDialog::updateMinorTickEnabled() {
    if (!m_minorSameCheck) return;
    const bool same = m_minorSameCheck->isChecked();
    m_minorCheck->setEnabled(same);
    m_minorNumCombo->setEnabled(same && m_minorCheck->isChecked());
    auto axisRow = [&](QCheckBox *check, QSpinBox *spin) {
        if (!check || !spin) return;
        check->setEnabled(!same);
        spin->setEnabled(!same && check->isChecked());
    };
    axisRow(m_xMinorCheck, m_xMinorNum);
    axisRow(m_yMinorCheck, m_yMinorNum);
    axisRow(m_y2MinorCheck, m_y2MinorNum);
}

void LatexExportDialog::updateZeroFillEnabled() {
    bool allDefault = m_xdecDefaultCheck->isChecked() && m_ydecDefaultCheck->isChecked()
                      && (!m_y2decDefaultCheck || m_y2decDefaultCheck->isChecked());
    m_zeroFillCheck->setEnabled(!allDefault);
    m_xdecSpin->setEnabled(!m_xdecDefaultCheck->isChecked());
    m_ydecSpin->setEnabled(!m_ydecDefaultCheck->isChecked());
    if (m_y2decSpin) m_y2decSpin->setEnabled(!m_y2decDefaultCheck->isChecked());
}

// The line plot keeps the original "latexExport" group; bars and histograms get their own,
// so each plot type remembers its own ticks / style / export settings.
QString LatexExportDialog::settingsGroup() const {
    return m_plotType == "line" ? QString("latexExport") : QString("latexExport/") + m_plotType;
}

// Forgets the saved settings of the current plot type only (the other plot types keep theirs)
// and closes the dialog; MainWindow::exportLatex() then opens a fresh one, which starts from
// the built-in defaults.
void LatexExportDialog::resetSettings() {
    QString what = m_plotType == "line" ? "line / scatter plot" : m_plotType == "bar" ? "bar chart" : "histogram";
    if (QMessageBox::question(this, "Reset settings",
                              "Set all export settings of the " + what + " back to their defaults?\n"
                              "The settings of the other plot types are not touched.") != QMessageBox::Yes)
        return;
    QSettings s;
    s.beginGroup(settingsGroup());
    // only this group's own keys: the line plot's group is the parent of the bar / histogram ones
    for (const QString &key : s.childKeys()) s.remove(key);
    s.endGroup();
    m_resetRequested = true;
    reject();
}

void LatexExportDialog::saveSettings() {
    if (m_resetRequested) return;   // keep the defaults that were just restored
    QSettings s;
    s.beginGroup(settingsGroup());
    // ticks & number format
    s.setValue("xtickStep", m_xtickEdit->text());
    s.setValue("ytickStep", m_ytickEdit->text());
    s.setValue("xdec", m_xdecSpin->value());
    s.setValue("ydec", m_ydecSpin->value());
    s.setValue("xdecDefault", m_xdecDefaultCheck->isChecked());
    s.setValue("ydecDefault", m_ydecDefaultCheck->isChecked());
    if (m_y2decSpin) {
        s.setValue("y2dec", m_y2decSpin->value());
        s.setValue("y2decDefault", m_y2decDefaultCheck->isChecked());
    }
    s.setValue("minorTicks", m_minorCheck->isChecked());
    s.setValue("minorTickNum", m_minorNumCombo->value());
    s.setValue("minorSame", m_minorSameCheck->isChecked());
    s.setValue("minorX", m_xMinorCheck->isChecked());
    s.setValue("minorXNum", m_xMinorNum->value());
    s.setValue("minorY", m_yMinorCheck->isChecked());
    s.setValue("minorYNum", m_yMinorNum->value());
    if (m_y2MinorCheck) {
        s.setValue("minorY2", m_y2MinorCheck->isChecked());
        s.setValue("minorY2Num", m_y2MinorNum->value());
    }
    s.setValue("tickAlign", m_tickAlignCombo->currentText());
    s.setValue("zeroFill", m_zeroFillCheck->isChecked());
    s.setValue("hideTicks", m_hideTicksCheck->isChecked());
    s.setValue("xtickPos", m_xtickPosCombo->currentText());
    s.setValue("ytickPos", m_ytickPosCombo->currentText());
    // style
    s.setValue("grid", m_gridModeCombo->currentText());
    s.setValue("smoothing", m_smoothingCombo->currentText());
    s.setValue("widthCm", m_widthEdit->text());
    s.setValue("heightCm", m_heightEdit->text());
    s.setValue("axisBox", m_axisBoxCheck->isChecked());
    s.setValue("escapeText", m_escapeTextCheck->isChecked());
    s.setValue("barWidth", m_barWidthEdit->text());
    s.setValue("xTickLabelRot", m_xLabelRotEdit->text());
    s.setValue("xTickLabelAnchor", m_xLabelAnchorCombo->currentText());
    s.setValue("yTickLabelRot", m_yLabelRotEdit->text());
    s.setValue("yTickLabelAnchor", m_yLabelAnchorCombo->currentText());
    if (m_y2LabelRotEdit) {
        s.setValue("y2TickLabelRot", m_y2LabelRotEdit->text());
        s.setValue("y2TickLabelAnchor", m_y2LabelAnchorCombo->currentText());
    }
    s.setValue("histTick", m_histTickCombo->currentIndex());
    // error bar look
    s.setValue("errDir", m_errDirCombo->currentText());
    s.setValue("errSameColor", m_errSameColorCheck->isChecked());
    s.setValue("errColor", m_errColorPicker->colorToken());
    s.setValue("errWidth", m_errWidthEdit->text());
    s.setValue("errStyle", m_errStyleCombo->currentText());
    s.setValue("errCap", m_errCapCombo->currentText());
    s.setValue("errCapSize", m_errCapSizeEdit->text());
    s.endGroup();
}

void LatexExportDialog::loadSettings() {
    QSettings s;
    s.beginGroup(settingsGroup());
    auto setCombo = [](QComboBox *cb, const QVariant &v) {
        int i = cb->findText(v.toString());
        if (i >= 0) cb->setCurrentIndex(i);
    };
    // Only override the built-in defaults when a value was actually saved.
    if (s.contains("xtickStep")) m_xtickEdit->setText(s.value("xtickStep").toString());
    if (s.contains("ytickStep")) m_ytickEdit->setText(s.value("ytickStep").toString());
    if (s.contains("xdec")) m_xdecSpin->setValue(s.value("xdec").toInt());
    if (s.contains("ydec")) m_ydecSpin->setValue(s.value("ydec").toInt());
    if (s.contains("xdecDefault")) m_xdecDefaultCheck->setChecked(s.value("xdecDefault").toBool());
    if (s.contains("ydecDefault")) m_ydecDefaultCheck->setChecked(s.value("ydecDefault").toBool());
    if (m_y2decSpin) {
        if (s.contains("y2dec")) m_y2decSpin->setValue(s.value("y2dec").toInt());
        if (s.contains("y2decDefault")) m_y2decDefaultCheck->setChecked(s.value("y2decDefault").toBool());
    }
    if (s.contains("minorTicks")) m_minorCheck->setChecked(s.value("minorTicks").toBool());
    if (s.contains("minorTickNum")) m_minorNumCombo->setValue(s.value("minorTickNum").toInt());
    if (s.contains("minorX")) m_xMinorCheck->setChecked(s.value("minorX").toBool());
    if (s.contains("minorXNum")) m_xMinorNum->setValue(s.value("minorXNum").toInt());
    if (s.contains("minorY")) m_yMinorCheck->setChecked(s.value("minorY").toBool());
    if (s.contains("minorYNum")) m_yMinorNum->setValue(s.value("minorYNum").toInt());
    if (m_y2MinorCheck) {
        if (s.contains("minorY2")) m_y2MinorCheck->setChecked(s.value("minorY2").toBool());
        if (s.contains("minorY2Num")) m_y2MinorNum->setValue(s.value("minorY2Num").toInt());
    }
    if (s.contains("minorSame")) m_minorSameCheck->setChecked(s.value("minorSame").toBool());
    if (s.contains("tickAlign")) setCombo(m_tickAlignCombo, s.value("tickAlign"));
    if (s.contains("zeroFill")) m_zeroFillCheck->setChecked(s.value("zeroFill").toBool());
    if (s.contains("hideTicks")) m_hideTicksCheck->setChecked(s.value("hideTicks").toBool());
    if (s.contains("xtickPos")) setCombo(m_xtickPosCombo, s.value("xtickPos"));
    if (s.contains("ytickPos")) setCombo(m_ytickPosCombo, s.value("ytickPos"));
    if (s.contains("grid")) setCombo(m_gridModeCombo, s.value("grid"));
    if (s.contains("smoothing")) setCombo(m_smoothingCombo, s.value("smoothing"));
    if (s.contains("widthCm")) m_widthEdit->setText(s.value("widthCm").toString());
    if (s.contains("heightCm")) m_heightEdit->setText(s.value("heightCm").toString());
    if (s.contains("axisBox")) m_axisBoxCheck->setChecked(s.value("axisBox").toBool());
    if (s.contains("escapeText")) m_escapeTextCheck->setChecked(s.value("escapeText").toBool());
    if (s.contains("barWidth")) m_barWidthEdit->setText(s.value("barWidth").toString());
    // (older versions saved one rotation / anchor for the bar category axis, which was X)
    if (s.contains("tickLabelRot")) m_xLabelRotEdit->setText(s.value("tickLabelRot").toString());
    if (s.contains("tickLabelAnchor")) setCombo(m_xLabelAnchorCombo, s.value("tickLabelAnchor"));
    if (s.contains("xTickLabelRot")) m_xLabelRotEdit->setText(s.value("xTickLabelRot").toString());
    if (s.contains("xTickLabelAnchor")) setCombo(m_xLabelAnchorCombo, s.value("xTickLabelAnchor"));
    if (s.contains("yTickLabelRot")) m_yLabelRotEdit->setText(s.value("yTickLabelRot").toString());
    if (s.contains("yTickLabelAnchor")) setCombo(m_yLabelAnchorCombo, s.value("yTickLabelAnchor"));
    if (m_y2LabelRotEdit) {
        if (s.contains("y2TickLabelRot")) m_y2LabelRotEdit->setText(s.value("y2TickLabelRot").toString());
        if (s.contains("y2TickLabelAnchor")) setCombo(m_y2LabelAnchorCombo, s.value("y2TickLabelAnchor"));
    }
    if (s.contains("histTick")) m_histTickCombo->setCurrentIndex(std::clamp(s.value("histTick").toInt(), 0, 2));
    if (s.contains("errDir")) setCombo(m_errDirCombo, s.value("errDir"));
    if (s.contains("errSameColor")) m_errSameColorCheck->setChecked(s.value("errSameColor").toBool());
    if (s.contains("errColor")) m_errColorPicker->setColorToken(s.value("errColor").toString());
    if (s.contains("errWidth")) m_errWidthEdit->setText(s.value("errWidth").toString());
    if (s.contains("errStyle")) setCombo(m_errStyleCombo, s.value("errStyle"));
    if (s.contains("errCap")) setCombo(m_errCapCombo, s.value("errCap"));
    if (s.contains("errCapSize")) m_errCapSizeEdit->setText(s.value("errCapSize").toString());
    s.endGroup();
}

// Runs external programs one after another in `workDir` (each with a 2-minute limit) and
// reports once: success (all exited 0), time-out, or the first failure, with the combined log.
namespace {
struct ProcStep { QString program; QStringList args; };

void runProcessChain(QObject *owner, const QString &workDir, const QVector<ProcStep> &steps,
                     std::function<void(bool ok, bool timedOut, const QString &log)> done) {
    auto idx = std::make_shared<int>(0);
    auto log = std::make_shared<QString>();
    auto run = std::make_shared<std::function<void()>>();
    *run = [=]() {
        if (*idx >= steps.size()) { done(true, false, *log); return; }
        const ProcStep st = steps[*idx];
        auto *proc = new QProcess(owner);
        proc->setWorkingDirectory(workDir);
        proc->setProgram(st.program);
        proc->setArguments(st.args);
        auto timedOut = std::make_shared<bool>(false);
        auto *timer = new QTimer(proc);
        timer->setSingleShot(true);
        timer->setInterval(120000);
        QObject::connect(timer, &QTimer::timeout, proc, [proc, timedOut]() { *timedOut = true; proc->kill(); });
        QObject::connect(proc, &QProcess::errorOccurred, owner, [=](QProcess::ProcessError err) {
            if (err != QProcess::FailedToStart) return;
            *log += QString("Could not start '%1'. Make sure it is installed and on the system PATH.\n").arg(st.program);
            proc->deleteLater();
            done(false, false, *log);
        });
        QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), owner,
                         [=](int exitCode, QProcess::ExitStatus status) {
            timer->stop();
            *log += QString("$ %1 %2\n").arg(st.program, st.args.join(' '))
                  + QString::fromLocal8Bit(proc->readAllStandardOutput()) + "\n"
                  + QString::fromLocal8Bit(proc->readAllStandardError()) + "\n";
            proc->deleteLater();
            if (status == QProcess::NormalExit && exitCode == 0) { ++*idx; (*run)(); }
            else done(false, *timedOut, *log);
        });
        timer->start();
        proc->start();
    };
    (*run)();
}
} // namespace

// -------------------------------------------------------------- generate
void LatexExportDialog::generate() {
    LatexExportOptions opts;
    try {
        opts = collectOpts();
    } catch (std::exception &e) {
        QMessageBox::critical(this, "Invalid input", e.what());
        return;
    }
    saveSettings();

    QString code = buildLatexDocument(opts);
    // SVG is a single figure, so it is always built from the standalone variant
    // (no page, margins or caption), even if "wrap in figure" is on.
    LatexExportOptions svgOpts = opts;
    svgOpts.wrapFigure = false;
    QString svgCode = buildLatexDocument(svgOpts);
    QMap<QString, QString> csvFiles;
    if (opts.dataSource == "csv") {
        try {
            csvFiles = buildCsvExportText(opts, m_app->datasets());
        } catch (std::exception &e) {
            QMessageBox::critical(this, "Couldn't build CSV data",
                QString("The LaTeX code was generated, but the accompanying CSV data (needed to "
                        "compile) could not be built:\n%1").arg(e.what()));
        }
    }

    auto *codeDlg = new QDialog(this);
    codeDlg->setAttribute(Qt::WA_DeleteOnClose);
    codeDlg->setWindowTitle("LaTeX / pgfplots code");
    codeDlg->resize(720, 640);
    auto *layout = new QVBoxLayout(codeDlg);

    auto *textEdit = new QTextEdit;
    textEdit->setReadOnly(true);
    textEdit->setFontFamily("monospace");
    textEdit->setPlainText(code);
    layout->addWidget(textEdit, 1);

    if (!csvFiles.isEmpty()) {
        auto *note = new QLabel(QString("This export also needs %1 accompanying CSV file(s), saved alongside the .tex file.").arg(csvFiles.size()));
        note->setStyleSheet("color:#555;");
        layout->addWidget(note);
    }

    auto *btnRow = new QHBoxLayout;
    auto *copyBtn = new QPushButton("Copy to clipboard");
    connect(copyBtn, &QPushButton::clicked, codeDlg, [code]() { QGuiApplication::clipboard()->setText(code); });
    btnRow->addWidget(copyBtn);

    auto *saveBtn = new QPushButton("Save .tex" + QString(csvFiles.isEmpty() ? "" : " (+ CSV) ..."));
    connect(saveBtn, &QPushButton::clicked, codeDlg, [this, code, csvFiles]() {
        QString startDir = m_app->lastSaveDir().isEmpty() ? QDir::homePath() : m_app->lastSaveDir();
        QString path = QFileDialog::getSaveFileName(this, "Save LaTeX file", startDir + "/figure.tex", "LaTeX files (*.tex)");
        if (path.isEmpty()) return;
        m_app->setLastSaveDir(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Save failed", "Could not write to that location.");
            return;
        }
        QTextStream(&f) << code;
        f.close();

        if (!csvFiles.isEmpty()) {
            QDir dir = QFileInfo(path).absoluteDir();
            for (auto it = csvFiles.constBegin(); it != csvFiles.constEnd(); ++it) {
                QFile cf(dir.filePath(it.key()));
                if (cf.open(QIODevice::WriteOnly | QIODevice::Text)) QTextStream(&cf) << it.value();
            }
        }
        QMessageBox::information(this, "Saved", "LaTeX (and any CSV data) written to:\n" + QFileInfo(path).absolutePath());
    });
    btnRow->addWidget(saveBtn);

    auto *compileBtn = new QPushButton("Compile to PDF...");
    btnRow->addWidget(compileBtn);
    auto *svgBtn = new QPushButton("Save as SVG...");
    svgBtn->setToolTip("Vector SVG straight from LaTeX (latex -> dvisvgm), no PDF needed");
    btnRow->addWidget(svgBtn);
    auto *statusLabel = new QLabel;
    statusLabel->setStyleSheet("color:#555;");
    btnRow->addWidget(statusLabel);

    connect(compileBtn, &QPushButton::clicked, codeDlg, [this, codeDlg, code, csvFiles, compileBtn, statusLabel]() {
        QString engine = findLatexEngine();
        if (engine.isEmpty()) {
            QMessageBox::critical(codeDlg, "No LaTeX installation found",
                "Couldn't find lualatex, latexmk, pdflatex, or xelatex on your system PATH.\n\n"
                "Install a LaTeX distribution (e.g. TeX Live, MiKTeX, or MacTeX) to compile here, "
                "or use \"Save .tex\" and compile it elsewhere (e.g. Overleaf).");
            return;
        }

        QString tmpBase = QDir::temp().filePath(QString("uplotter_tex_%1").arg(QDateTime::currentMSecsSinceEpoch()));
        if (!QDir().mkpath(tmpBase)) {
            QMessageBox::critical(codeDlg, "Compile failed", "Could not create a temporary directory.");
            return;
        }
        QString texPath = QDir(tmpBase).filePath("figure.tex");
        QString pdfPath = QDir(tmpBase).filePath("figure.pdf");
        {
            QFile texFile(texPath);
            if (!texFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QMessageBox::critical(codeDlg, "Compile failed", "Could not write the temporary .tex file.");
                return;
            }
            QTextStream(&texFile) << code;
        }
        for (auto it = csvFiles.constBegin(); it != csvFiles.constEnd(); ++it) {
            QFile cf(QDir(tmpBase).filePath(it.key()));
            if (cf.open(QIODevice::WriteOnly | QIODevice::Text)) QTextStream(&cf) << it.value();
        }

        QString program;
        QStringList args;
        if (engine == "latexmk") {
            // Force latexmk onto the lualatex engine rather than its default
            // pdflatex, matching the Python app's engine preference order.
            program = "latexmk";
            args = { "-lualatex", "-interaction=nonstopmode", "-halt-on-error", "figure.tex" };
        } else {
            program = engine;
            args = { "-interaction=nonstopmode", "-halt-on-error", "figure.tex" };
        }

        compileBtn->setEnabled(false);
        statusLabel->setText(QString("Compiling with %1...").arg(engine));

        auto *proc = new QProcess(codeDlg);
        proc->setWorkingDirectory(tmpBase);
        proc->setProgram(program);
        proc->setArguments(args);

        auto timedOutFlag = std::make_shared<bool>(false);
        auto *timeoutTimer = new QTimer(proc);
        timeoutTimer->setSingleShot(true);
        timeoutTimer->setInterval(120000);
        connect(timeoutTimer, &QTimer::timeout, proc, [proc, timedOutFlag]() { *timedOutFlag = true; proc->kill(); });

        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), codeDlg,
            [this, codeDlg, proc, compileBtn, statusLabel, tmpBase, pdfPath, timeoutTimer, timedOutFlag]
            (int exitCode, QProcess::ExitStatus exitStatus) {
                timeoutTimer->stop();
                compileBtn->setEnabled(true);
                statusLabel->setText("");
                QString log = QString::fromLocal8Bit(proc->readAllStandardOutput())
                             + "\n" + QString::fromLocal8Bit(proc->readAllStandardError());
                bool ok = (exitStatus == QProcess::NormalExit) && (exitCode == 0) && QFile::exists(pdfPath);

                if (ok) {
                    QString startDir = m_app->lastSaveDir().isEmpty() ? QDir::homePath() : m_app->lastSaveDir();
                    QString dest = QFileDialog::getSaveFileName(codeDlg, "Save compiled PDF", startDir + "/figure.pdf", "PDF files (*.pdf)");
                    if (!dest.isEmpty()) {
                        m_app->setLastSaveDir(QFileInfo(dest).absolutePath());
                        QFile::remove(dest);
                        if (QFile::copy(pdfPath, dest)) {
                            auto reply = QMessageBox::question(codeDlg, "Compiled",
                                "PDF saved to:\n" + dest + "\n\nOpen it now?", QMessageBox::Yes | QMessageBox::No);
                            if (reply == QMessageBox::Yes) QDesktopServices::openUrl(QUrl::fromLocalFile(dest));
                        } else {
                            QMessageBox::critical(codeDlg, "Save failed", "Could not copy the compiled PDF to that location.");
                        }
                    }
                } else if (*timedOutFlag) {
                    QMessageBox::critical(codeDlg, "Compile failed", "Compilation timed out after 120 seconds.");
                } else {
                    showLatexLogWindow(codeDlg, log);
                }
                QDir(tmpBase).removeRecursively();
                proc->deleteLater();
            });

        connect(proc, &QProcess::errorOccurred, codeDlg,
            [codeDlg, proc, compileBtn, statusLabel, tmpBase, program](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart) return;
                compileBtn->setEnabled(true);
                statusLabel->setText("");
                QMessageBox::critical(codeDlg, "Compile failed",
                    QString("Could not start '%1'. Make sure your LaTeX installation's bin "
                            "directory is on the system PATH.").arg(program));
                QDir(tmpBase).removeRecursively();
                proc->deleteLater();
            });

        timeoutTimer->start();
        proc->start();
    });

    // ---- SVG export ----
    // Preferred route needs no PDF at all: latex -> DVI -> dvisvgm (text becomes outlines).
    // If those two aren't installed, fall back to PDF -> SVG with pdf2svg or Inkscape.
    connect(svgBtn, &QPushButton::clicked, codeDlg,
            [this, codeDlg, svgCode, csvFiles, svgBtn, compileBtn, statusLabel]() {
        QVector<ProcStep> steps;
        QString route;
        if (!QStandardPaths::findExecutable("latex").isEmpty() && !QStandardPaths::findExecutable("dvisvgm").isEmpty()) {
            route = "latex -> dvisvgm";
            steps = {
                { "latex", { "-interaction=nonstopmode", "-halt-on-error", "-jobname=figure",
                             "\\def\\pgfsysdriver{pgfsys-dvisvgm.def}\\input{figure.tex}" } },
                { "dvisvgm", { "--no-fonts", "--exact-bbox", "figure.dvi", "-o", "figure.svg" } }
            };
        } else {
            QString engine = findLatexEngine();
            QString converter = !QStandardPaths::findExecutable("pdf2svg").isEmpty() ? "pdf2svg"
                              : !QStandardPaths::findExecutable("inkscape").isEmpty() ? "inkscape" : QString();
            if (engine.isEmpty() || converter.isEmpty()) {
                QMessageBox::critical(codeDlg, "Can't create SVG",
                    "SVG export needs 'latex' and 'dvisvgm' on your system PATH. Both are included in "
                    "TeX Live and MiKTeX (in MiKTeX, install the 'dvisvgm' package if it is missing).\n\n"
                    "Alternatively install pdf2svg or Inkscape, or use \"Save .tex\" and convert elsewhere.");
                return;
            }
            route = "PDF -> " + converter;
            if (engine == "latexmk")
                steps.push_back({ "latexmk", { "-lualatex", "-interaction=nonstopmode", "-halt-on-error", "figure.tex" } });
            else
                steps.push_back({ engine, { "-interaction=nonstopmode", "-halt-on-error", "figure.tex" } });
            if (converter == "pdf2svg") steps.push_back({ "pdf2svg", { "figure.pdf", "figure.svg" } });
            else steps.push_back({ "inkscape", { "figure.pdf", "--export-type=svg", "--export-filename=figure.svg" } });
        }

        QString tmpBase = QDir::temp().filePath(QString("uplotter_svg_%1").arg(QDateTime::currentMSecsSinceEpoch()));
        if (!QDir().mkpath(tmpBase)) {
            QMessageBox::critical(codeDlg, "SVG export failed", "Could not create a temporary directory.");
            return;
        }
        {
            QFile texFile(QDir(tmpBase).filePath("figure.tex"));
            if (!texFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QMessageBox::critical(codeDlg, "SVG export failed", "Could not write the temporary .tex file.");
                return;
            }
            QTextStream(&texFile) << svgCode;
        }
        for (auto it = csvFiles.constBegin(); it != csvFiles.constEnd(); ++it) {
            QFile cf(QDir(tmpBase).filePath(it.key()));
            if (cf.open(QIODevice::WriteOnly | QIODevice::Text)) QTextStream(&cf) << it.value();
        }

        svgBtn->setEnabled(false);
        compileBtn->setEnabled(false);
        statusLabel->setText(QString("Creating SVG (%1)...").arg(route));

        QString svgPath = QDir(tmpBase).filePath("figure.svg");
        runProcessChain(codeDlg, tmpBase, steps,
            [this, codeDlg, svgBtn, compileBtn, statusLabel, tmpBase, svgPath](bool ok, bool timedOut, const QString &log) {
                svgBtn->setEnabled(true);
                compileBtn->setEnabled(true);
                statusLabel->setText("");
                if (ok && QFile::exists(svgPath)) {
                    QString startDir = m_app->lastSaveDir().isEmpty() ? QDir::homePath() : m_app->lastSaveDir();
                    QString dest = QFileDialog::getSaveFileName(codeDlg, "Save SVG", startDir + "/figure.svg", "SVG files (*.svg)");
                    if (!dest.isEmpty()) {
                        if (QFileInfo(dest).suffix().isEmpty()) dest += ".svg";
                        m_app->setLastSaveDir(QFileInfo(dest).absolutePath());
                        QFile::remove(dest);
                        if (QFile::copy(svgPath, dest)) {
                            auto reply = QMessageBox::question(codeDlg, "SVG saved",
                                "SVG saved to:\n" + dest + "\n\nOpen it now?", QMessageBox::Yes | QMessageBox::No);
                            if (reply == QMessageBox::Yes) QDesktopServices::openUrl(QUrl::fromLocalFile(dest));
                        } else {
                            QMessageBox::critical(codeDlg, "Save failed", "Could not copy the SVG to that location.");
                        }
                    }
                } else if (timedOut) {
                    QMessageBox::critical(codeDlg, "SVG export failed", "Timed out after 120 seconds.");
                } else {
                    showLatexLogWindow(codeDlg, log);
                }
                QDir(tmpBase).removeRecursively();
            });
    });

    auto *closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, codeDlg, &QDialog::close);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    codeDlg->show();
}

QString LatexExportDialog::findLatexEngine() {
    static const QStringList candidates = { "lualatex", "latexmk", "pdflatex", "xelatex" };
    for (const QString &c : candidates) {
        if (!QStandardPaths::findExecutable(c).isEmpty()) return c;
    }
    return QString();
}

void LatexExportDialog::showLatexLogWindow(QWidget *logParent, const QString &log) {
    auto *dlg = new QDialog(logParent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString::fromUtf8("Compilation failed \u2014 log"));
    dlg->resize(700, 500);
    auto *layout = new QVBoxLayout(dlg);

    auto *info = new QLabel("Compilation failed. Full engine log below (scroll for details):");
    layout->addWidget(info);

    auto *logEdit = new QTextEdit;
    logEdit->setReadOnly(true);
    logEdit->setFontFamily("monospace");
    logEdit->setPlainText(log);
    layout->addWidget(logEdit, 1);

    auto *btnRow = new QHBoxLayout;
    auto *copyBtn = new QPushButton("Copy log");
    connect(copyBtn, &QPushButton::clicked, dlg, [log]() { QGuiApplication::clipboard()->setText(log); });
    btnRow->addWidget(copyBtn);
    auto *closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    dlg->show();
}
