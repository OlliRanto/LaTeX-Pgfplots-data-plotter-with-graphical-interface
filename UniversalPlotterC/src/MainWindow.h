#pragma once
#include <QMainWindow>
#include <QVector>
#include <QMap>
#include <QPointer>
#include "ColorSchemes.h"
#include "qcustomplot.h"
#include "Dataset.h"
#include "CurveFit.h"
#include "DraggableVLine.h"

class QComboBox;
class QLineEdit;
class QListWidget;
class QCheckBox;
class QLabel;
class QTextEdit;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QGroupBox;
class QVBoxLayout;
class QMouseEvent;
class QTimer;
class DataEntryDialog;

struct FitSlot { double xmin = 0, xmax = 0; QVector<CurveKey> cols; };
struct IntegralSlot { double xmin = 0, xmax = 0; QVector<QPair<CurveKey, QString>> items; };

using FitResultKey = QPair<CurveKey, int>;               // (y_col, slot_idx)
using IntegralResultKey = QPair<QPair<CurveKey, QString>, int>; // ((y_col,source), slot_idx)

struct IntegralResultData {
    double area = 0;
    QVector<double> xs, ys; // raw curve points within range (unclosed)
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // ---- read-only accessors used by LatexExportDialog ----
    const QVector<Dataset> &datasets() const { return m_datasets; }
    const QVector<CurveKey> &currentYCols() const { return m_currentYCols; }
    QString curveLabelPublic(const CurveKey &key) const { return curveLabelPlain(key); } // no "(Y2)" tag

    // ---- plot type (line / bar / histogram) ----
    enum class PlotType { Line, Bar, Histogram };
    PlotType plotType() const;
    PlotType plottedType() const { return m_plottedType; }       // type of what is on screen now
    // Text labels of the X column when it is non-numeric (bar-chart categories); else empty.
    QStringList categoryLabelsPublic(const CurveKey &key) const { return categoryLabels(key); }
    // Bin edges (n+1) and bar heights (n) of the last plotted histogram for this curve.
    bool histogramPublic(const CurveKey &key, QVector<double> &edges, QVector<double> &heights) const;
    QString histNormalizationName() const;                      // "Count" / "Probability" / "Density"
    bool secondAxisEnabled() const { return m_y2Check && m_y2Check->isChecked(); }
    bool secondAxisInUse() const {
        for (const auto &k : m_currentYCols) if (k.y2) return true;
        return false;
    }
    // The axis that carries the second set of VALUES: the right-hand axis normally, the top
    // axis when bars are drawn sideways.
    QCPAxis *secondValueAxis() const { return m_flipBars ? m_plot->xAxis2 : m_plot->yAxis2; }
    QString y2AxisLabelText() const { return m_plot ? secondValueAxis()->label() : QString(); }
    void currentY2Range(double &lo, double &hi) const {
        if (m_plot) { lo = secondValueAxis()->range().lower; hi = secondValueAxis()->range().upper; }
        else { lo = 0; hi = 1; }
    }
    bool barNumbersAsText() const { return m_numAsTextBars; }   // plotted bars treat numeric X values as evenly spaced text
    // Color scheme the curves on screen were drawn with (the export starts from the same one),
    // and the gap between the bars of one group (fraction of a bar's width) of the last plot.
    QString plottedColorScheme() const { return m_plottedScheme.isEmpty() ? defaultColorSchemeName() : m_plottedScheme; }
    double plottedBarGap() const { return m_plottedBarGap; }
    bool barsFlipped() const { return m_flipBars; }   // plotted bars / histogram bins are horizontal
    int histTickModePublic() const { return m_histTickCombo ? m_histTickCombo->currentIndex() : 0; } // 0 auto, 1 edges, 2 centers
    bool logY2Enabled() const { return m_logY2Check && m_logY2Check->isChecked(); }
    QVector<double> curveXArrayPublic(const CurveKey &key) const { return curveXArray(key); }
    QVector<double> curveYArrayPublic(const CurveKey &key) const { return curveYArray(key); }
    QColor curveColor(const CurveKey &key) const {
        if (m_seriesColors.contains(key)) return m_seriesColors[key];   // bar / histogram series
        return m_dataGraphs.contains(key) ? m_dataGraphs[key]->pen().color() : QColor("#1f77b4");
    }
    const QMap<FitResultKey, FitResult> &fitResultsPublic() const { return m_fitResults; }
    const QMap<IntegralResultKey, IntegralResultData> &integralResultsPublic() const { return m_integralResults; }
    bool fitEnabled() const { return m_fitEnableCheck && m_fitEnableCheck->isChecked(); }
    bool integralEnabled() const { return m_integralEnableCheck && m_integralEnableCheck->isChecked(); }
    QString plotTitleText() const;
    QString xAxisLabelText() const { return m_plot ? m_plot->xAxis->label() : QString(); }
    QString yAxisLabelText() const { return m_plot ? m_plot->yAxis->label() : QString(); }
    void currentXRange(double &lo, double &hi) const;
    void currentYRange(double &lo, double &hi) const;
    bool logXEnabled() const { return m_logXCheck && m_logXCheck->isChecked(); }
    bool logYEnabled() const { return m_logYCheck && m_logYCheck->isChecked(); }
    bool gridEnabled() const { return m_gridCheck && m_gridCheck->isChecked(); }
    // ---- error bars (symmetric +/- std-dev on Y) ----
    bool errorBarsEnabled() const { return m_errBarCheck && m_errBarCheck->isChecked(); }
    // Name of the std-dev column chosen for this curve ("" = none).
    QString errorColumnNamePublic(const CurveKey &key) const { return errorColumnFor(key); }
    // Std-dev values aligned 1:1 with curveYArrayPublic(key); empty when error bars are
    // off or no std-dev column is selected for this curve.
    QVector<double> curveErrArrayPublic(const CurveKey &key) const { return curveErrArray(key); }
    QString currentStyleText() const { return m_styleCombo ? m_styleCombo->currentText() : "Line"; }

    // ---- persisted "last used folder" for file dialogs, remembered across
    // app restarts via QSettings. Open (data files) and save/compile
    // (LaTeX export, compiled PDF, plot image) each track their own folder,
    // since those are usually different locations in practice. ----
    QString lastOpenDir() const { return m_lastOpenDir; }
    void setLastOpenDir(const QString &dir);
    QString lastSaveDir() const { return m_lastSaveDir; }
    void setLastSaveDir(const QString &dir);

private slots:
    void openFiles();
    void openDataEntry();
    void addManualDataset(const Dataset &ds);
    void reparseFiles();
    void plot();
    void clearPlot();
    void autoscaleAxes();
    void applyAxisLimits();

    // ---- matplotlib-style navigation toolbar ----
    void setPanMode();
    void setZoomRectMode();
    void goHome();
    void goBack();
    void goForward();

    void onFitEnableToggled(bool on);
    void onFitTypeChanged();
    void onApplyFitRangeEntry();
    void onNewFitInterval();
    void onPrevFitInterval();
    void onNextFitInterval();
    void onFitColumnSelectionChanged();

    void onIntegralEnableToggled(bool on);
    void onApplyIntegralRangeEntry();
    void onNewIntegralInterval();
    void onPrevIntegralInterval();
    void onNextIntegralInterval();
    void onIntegralSelectionChanged();

    void onInterpEnableToggled(bool on);
    void onApplyInterpEntry();

    void exportLatex();
    void exportImage();

    void onPlotMousePressed(QMouseEvent *event);

private:
    // ---- setup ----
    void buildUi();
    QGroupBox *buildParsingBox();
    QGroupBox *buildColumnsBox();
    QGroupBox *buildPlotOptionsBox();
    QGroupBox *buildFitBox();
    QGroupBox *buildIntegralBox();
    QGroupBox *buildInterpBox();
    QGroupBox *buildFunctionsBox();   // line plot: plot f(x) over a domain, sampled at N points
    QGroupBox *buildAxisLimitsBox();
    QGroupBox *buildExportBox();

    // ---- data loading ----
    void loadFiles(const QStringList &paths);
    void refreshDatasetInfo();   // top-bar file label + parsing info text for m_datasets
    void populateColumnWidgets();
    // ---- plotted functions (each one is a Dataset with isFunction set) ----
    QVector<int> functionDatasetIndices() const;
    bool readFunctionInputs(QString &expr, double &lo, double &hi, int &n);
    void refreshFunctionList(int selectRow = -1);
    void rebuildColumnsKeepSelection(int removedDsIdx = -1);
    void addFunction();
    void updateFunction();
    void removeFunction();
    void onFunctionSelected();
    void rebuildXRows();
    void rebuildYList();
    void rebuildY2List();

    // ---- plotting helpers ----
    QString curveLabel(const CurveKey &key) const;       // includes a "[Y2]" tag for second-axis curves
    QString curveLabelPlain(const CurveKey &key) const;  // same without the tag
    QCPAxis *valueAxisFor(const CurveKey &key) const;    // yAxis or yAxis2
    QVector<double> curveXArray(const CurveKey &key) const;
    QVector<double> curveXArrayRaw(const CurveKey &key) const;   // X exactly as loaded (no "numbers as text" mapping)
    QVector<double> curveYArray(const CurveKey &key) const;
    QVector<double> curveErrArray(const CurveKey &key) const;
    void onPlotTypeChanged();
    bool buildBarSeries(const QVector<CurveKey> &allSel, PlotType type);
    QStringList categoryLabels(const CurveKey &key) const;
    void rebuildErrRows();                 // one std-dev selector per selected Y curve
    QString errorColumnFor(const CurveKey &key) const;
    void removeErrorBars();
    void removeAllOverlays(); // draggable lines, fit/integral/interp artifacts
    void populateFitAndIntegralLists();
    void rebuildFitListWidget();
    void rebuildIntegralListWidget();

    // ---- navigation toolbar helpers ----
    QWidget *buildNavToolbar();
    void syncAxisLimitEdits();          // reflect current axis ranges into the xlim/ylim edit boxes
    void resetViewHistory();            // called after a fresh plot(); clears history to the initial view
    void pushViewHistory();             // records the current view as a new history entry
    struct ViewState {
        QCPRange x, y, y2;
        bool operator==(const ViewState &o) const { return x == o.x && y == o.y && y2 == o.y2; }
    };
    void restoreView(const ViewState &view);
    void updateNavButtonsEnabled();

    // ---- fit feature ----
    void startFitPlacement();
    void cancelFitPlacement();
    void onPlotClickedForFit(QMouseEvent *event);
    void beginNewFitInterval(double x0, double x1);
    void showFitInterval(int idx);
    void removeFitDraggables();
    void removeFitIntervalArtifacts(int idx); // removes drawn fit lines for idx (all columns)
    void computeAndDrawFit();
    QVector<CurveKey> selectedFitColumns() const;
    void updateFitNavLabel();
    void clearFitResultsText();

    // ---- integral feature ----
    void startIntegralPlacement();
    void cancelIntegralPlacement();
    void onPlotClickedForIntegral(QMouseEvent *event);
    void beginNewIntegralInterval(double x0, double x1);
    void showIntegralInterval(int idx);
    void removeIntegralDraggables();
    void removeIntegralIntervalArtifacts(int idx);
    void computeAndDrawIntegral();
    QVector<double> snapValuesForCurrentPlot() const;
    IntegralResultData integrateData(const QVector<double> &x, const QVector<double> &y, double xmin, double xmax) const;
    void updateIntegralNavLabel();
    void clearIntegralResultsText();
    bool integralUsesFit() const;

    // ---- interpolation feature ----
    void startInterpPlacement();
    void cancelInterpPlacement();
    void onPlotClickedForInterp(QMouseEvent *event);
    void computeAndDrawInterp();
    void removeInterpArtifacts();
    void clearInterpResultsText();

    // ---- data ----
    QVector<Dataset> m_datasets;
    QVector<CurveKey> m_currentYCols;
    QMap<CurveKey, QCPGraph *> m_dataGraphs;
    QVector<QPointer<QCPErrorBars>> m_errorBars;
    QVector<QColor> m_plotColorCycle;

    // ---- fit state ----
    QVector<FitSlot> m_fitSlots;
    int m_activeFitSlot = -1;
    DraggableVLineGroup *m_fitVLines = nullptr;
    QMap<FitResultKey, FitResult> m_fitResults;
    QMap<FitResultKey, QCPGraph *> m_fitGraphs;
    QVector<double> m_fitPendingXs;
    bool m_fitPlacementActive = false;

    // ---- integral state ----
    QVector<IntegralSlot> m_integralSlots;
    int m_activeIntegralSlot = -1;
    DraggableVLineGroup *m_integralVLines = nullptr;
    QMap<IntegralResultKey, IntegralResultData> m_integralResults;
    QMap<IntegralResultKey, QCPCurve *> m_integralFills;
    QVector<double> m_integralPendingXs;
    bool m_integralPlacementActive = false;
    QVector<QPair<CurveKey, QString>> m_integralItems; // parallel to list widget rows

    // ---- interpolation state ----
    DraggableVLineGroup *m_interpVLine = nullptr;
    QMap<CurveKey, QCPGraph *> m_interpMarkers;
    bool m_interpPlacementActive = false;

    // ---- widgets ----
    QCustomPlot *m_plot = nullptr;
    DataEntryDialog *m_dataEntryDlg = nullptr;
    QLabel *m_fileLabel = nullptr;
    QLabel *m_infoLabel = nullptr;
    QComboBox *m_sepCombo = nullptr;
    QComboBox *m_decimalCombo = nullptr;

    QWidget *m_xRowsContainer = nullptr;
    QVBoxLayout *m_xRowsLayout = nullptr;
    struct XRow { int dsIdx; QComboBox *combo; };
    QVector<XRow> m_xRows;

    QListWidget *m_yList = nullptr;
    QGroupBox *m_funcBox = nullptr;             // "Functions" group (line plots only)
    QLineEdit *m_funcExprEdit = nullptr, *m_funcMinEdit = nullptr, *m_funcMaxEdit = nullptr;
    QSpinBox *m_funcSamplesSpin = nullptr;
    QListWidget *m_funcList = nullptr;
    QPushButton *m_funcUpdateBtn = nullptr, *m_funcRemoveBtn = nullptr;
    QVector<CurveKey> m_yItems;
    // ---- plot type ----
    QComboBox *m_plotTypeCombo = nullptr;
    PlotType m_plottedType = PlotType::Line;
    QLabel *m_styleLabel = nullptr;
    QWidget *m_barOptFrame = nullptr, *m_histOptFrame = nullptr;
    QDoubleSpinBox *m_barGapSpin = nullptr;     // gap between the bars of one group, as a fraction of one bar's width
    double m_plottedBarGap = 0.0;
    QComboBox *m_colorSchemeCombo = nullptr;    // color scheme of the plotted curves
    QString m_plottedScheme;
    QDoubleSpinBox *m_barWidthSpin = nullptr;   // bar width as a fraction of the spacing between bars
    QSpinBox *m_histBinsSpin = nullptr;
    QComboBox *m_histNormCombo = nullptr;
    QGroupBox *m_fitBox = nullptr, *m_integralBox = nullptr, *m_interpBox = nullptr;
    QVector<QPointer<QCPBars>> m_bars;
    QPointer<QCPBarsGroup> m_barsGroup;
    QMap<CurveKey, QPair<QVector<double>, QVector<double>>> m_histData;   // edges, heights
    QMap<CurveKey, QColor> m_seriesColors;                                // colour of each bar / histogram series
    bool m_flipBars = false;              // the bars on screen are horizontal
    QCheckBox *m_numAsTextCheck = nullptr;   // bar chart: treat numeric X values as text (even spacing)
    bool m_numAsTextBars = false;            // the bars on screen use that option
    QMap<double, int> m_textPosMap;          // X value -> bar position (0, 1, 2, ...) while m_numAsTextBars
    QStringList m_textPosLabels;             // tick label of every position while m_numAsTextBars
    QCheckBox *m_flipCheck = nullptr;
    QComboBox *m_histTickCombo = nullptr; // histogram X ticks: auto / bin edges / bin centres
    QCheckBox *m_histFlipCheck = nullptr; // histogram: sideways (kept in step with m_flipCheck)
    QLabel *m_xRowsLabel = nullptr;
    QCheckBox *m_y2Check = nullptr;       // "Second Y axis"
    QWidget *m_y2Frame = nullptr;         // holds the Y2 column selector (visible when ticked)
    QListWidget *m_y2List = nullptr;
    QVector<CurveKey> m_y2Items;
    QCheckBox *m_logY2Check = nullptr;
    QLineEdit *m_y2limMinEdit = nullptr, *m_y2limMaxEdit = nullptr;
    QVector<QWidget *> m_y2limWidgets;    // Y2 limit row, shown only while the second axis is on

    QComboBox *m_styleCombo = nullptr;
    QCheckBox *m_logXCheck = nullptr;
    QCheckBox *m_logYCheck = nullptr;
    QCheckBox *m_gridCheck = nullptr;
    QCheckBox *m_snapCheck = nullptr;
    QCheckBox *m_errBarCheck = nullptr;
    QWidget *m_errColFrame = nullptr;
    QVBoxLayout *m_errRowsLayout = nullptr;
    QMap<CurveKey, QString> m_errColForCurve;   // chosen std-dev column per curve

    QCheckBox *m_fitEnableCheck = nullptr;
    QComboBox *m_fitTypeCombo = nullptr;
    QWidget *m_polyFrame = nullptr;
    QSpinBox *m_polyDegreeSpin = nullptr;
    QWidget *m_customFrame = nullptr;
    QLineEdit *m_customExprEdit = nullptr;
    QLineEdit *m_customParamsEdit = nullptr;
    QLineEdit *m_customGuessEdit = nullptr;
    QLineEdit *m_fitXminEdit = nullptr;
    QLineEdit *m_fitXmaxEdit = nullptr;
    QLabel *m_fitNavLabel = nullptr;
    QListWidget *m_fitColList = nullptr;
    QTextEdit *m_fitResultsText = nullptr;

    QCheckBox *m_integralEnableCheck = nullptr;
    QLineEdit *m_integralXminEdit = nullptr;
    QLineEdit *m_integralXmaxEdit = nullptr;
    QLabel *m_integralNavLabel = nullptr;
    QListWidget *m_integralList = nullptr;
    QTextEdit *m_integralResultsText = nullptr;

    QCheckBox *m_interpEnableCheck = nullptr;
    QLineEdit *m_interpXEdit = nullptr;
    QTextEdit *m_interpResultsText = nullptr;

    QLineEdit *m_xlimMinEdit = nullptr;
    QLineEdit *m_xlimMaxEdit = nullptr;
    QLineEdit *m_ylimMinEdit = nullptr;
    QLineEdit *m_ylimMaxEdit = nullptr;

    // ---- matplotlib-style navigation toolbar ----
    QPushButton *m_panBtn = nullptr;
    QPushButton *m_zoomBtn = nullptr;
    QPushButton *m_homeBtn = nullptr;
    QPushButton *m_backBtn = nullptr;
    QPushButton *m_forwardBtn = nullptr;
    QVector<ViewState> m_viewHistory; // (x, y, y2 ranges) per entry
    int m_viewHistoryPos = -1;
    bool m_restoringView = false; // guard so restoring a view doesn't re-push history
    QTimer *m_historyDebounceTimer = nullptr; // coalesces bursts of range changes (drag/wheel) into one history entry

    // ---- folder memory for file dialogs (persisted via QSettings) ----
    QString m_lastOpenDir;  // "open data file(s)" dialog
    QString m_lastSaveDir;  // shared across "save image", "save .tex", "save compiled pdf"
};
