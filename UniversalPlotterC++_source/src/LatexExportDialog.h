#pragma once
#include <QDialog>
#include <QVector>
#include <QMap>
#include "Dataset.h"
#include "CurveFit.h"
#include "LatexExport.h"
#include "MainWindow.h"

class MainWindow;
class QGroupBox;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QRadioButton;
class QLabel;
class QSpinBox;
class ColorPickerWidget;

// Full-featured "Export to LaTeX (pgfplots)" dialog, mirroring
// LatexExportDialog from the Python app section-for-section: data source
// (embed vs CSV), labels & axis limits, ticks & number format, style
// (grid/smoothing/size/axis box), legend, per-curve color/marker/line,
// curve fit styling, integral shading, and an optional \figure wrapper.
class LatexExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit LatexExportDialog(MainWindow *app, QWidget *parent = nullptr);
    bool resetRequested() const { return m_resetRequested; }   // closed by "Reset settings": open a fresh dialog

private slots:
    void onSourceChanged();
    void onLegendModeChanged();
    void onWrapFigureChanged();
    void generate();
    void updateZeroFillEnabled();
    void updateMinorTickEnabled();
    void applyColorScheme(int schemeIdx);   // colors the curve rows (in their current order) with a scheme
    void markSchemeCustom();                // the user changed a color by hand
    QGroupBox *buildAxisColorsSection();
    void resetSettings();
    void saveSettings();
    void loadSettings();

private:
    QGroupBox *buildSourceSection();
    QGroupBox *buildLabelsLimitsSection();
    QGroupBox *buildTicksSection();
    QGroupBox *buildStyleSection();
    QGroupBox *buildLegendSection();
    QGroupBox *buildCurvesSection();
    QGroupBox *buildErrorBarsSection();
    QGroupBox *buildBarSection();
    QGroupBox *buildFitSection();
    QGroupBox *buildIntegralSection();
    QGroupBox *buildFigureSection();

    // throws std::runtime_error with a user-facing message on bad input,
    // mirroring _collect_opts() raising ValueError
    LatexExportOptions collectOpts();

    // LaTeX compilation, mirroring LatexCodeWindow._compile_to_pdf() et al.
    static QString findLatexEngine();
    void showLatexLogWindow(QWidget *logParent, const QString &log);

    MainWindow *m_app;
    QMap<FitResultKey, FitResult> m_fitResults;
    bool m_hasFit = false;
    QMap<IntegralResultKey, IntegralResultData> m_integralResults;
    bool m_hasIntegral = false;

    // ---- data source ----
    QRadioButton *m_sourceEmbedRadio = nullptr;
    QRadioButton *m_sourceCsvRadio = nullptr;
    QWidget *m_csvFrame = nullptr;
    struct CsvDsRow { QLineEdit *filenameEdit; QComboBox *sepCombo; };
    QMap<int, CsvDsRow> m_csvDsRows;

    // ---- labels & limits ----
    QLineEdit *m_titleEdit = nullptr, *m_xlabelEdit = nullptr, *m_ylabelEdit = nullptr;
    QLineEdit *m_xminEdit = nullptr, *m_xmaxEdit = nullptr, *m_yminEdit = nullptr, *m_ymaxEdit = nullptr;
    QCheckBox *m_logxCheck = nullptr, *m_logyCheck = nullptr;
    QLineEdit *m_ylabelRotateEdit = nullptr;
    // second Y axis (only shown when curves are plotted on it)
    bool m_hasSecondAxis = false;
    QLineEdit *m_y2labelEdit = nullptr, *m_y2minEdit = nullptr, *m_y2maxEdit = nullptr, *m_y2tickEdit = nullptr;
    QLineEdit *m_y2labelRotateEdit = nullptr;
    QSpinBox *m_y2decSpin = nullptr;
    QCheckBox *m_y2decDefaultCheck = nullptr;
    QCheckBox *m_logy2Check = nullptr;
    QCheckBox *m_escapeTextCheck = nullptr;

    // ---- ticks ----
    QLineEdit *m_xtickEdit = nullptr, *m_ytickEdit = nullptr;
    QSpinBox *m_xdecSpin = nullptr, *m_ydecSpin = nullptr;
    QCheckBox *m_xdecDefaultCheck = nullptr, *m_ydecDefaultCheck = nullptr;
    // minor ticks: one shared setting ("same for all axes") or one per axis
    QCheckBox *m_minorSameCheck = nullptr;                          // use the shared setting for every axis
    QCheckBox *m_minorCheck = nullptr;                              // shared: minor ticks on / off
    QSpinBox *m_minorNumCombo = nullptr;                            // shared: minor ticks per interval
    QCheckBox *m_xMinorCheck = nullptr, *m_yMinorCheck = nullptr, *m_y2MinorCheck = nullptr;
    QSpinBox *m_xMinorNum = nullptr, *m_yMinorNum = nullptr, *m_y2MinorNum = nullptr;
    QComboBox *m_tickAlignCombo = nullptr;
    QCheckBox *m_xZeroFillCheck = nullptr, *m_yZeroFillCheck = nullptr, *m_y2ZeroFillCheck = nullptr;   // per axis
    QCheckBox *m_hideTicksCheck = nullptr;
    QComboBox *m_xtickPosCombo = nullptr, *m_ytickPosCombo = nullptr;

    // ---- style ----
    QComboBox *m_gridModeCombo = nullptr;
    QComboBox *m_smoothingCombo = nullptr;
    QLineEdit *m_widthEdit = nullptr, *m_heightEdit = nullptr;
    QCheckBox *m_axisBoxCheck = nullptr;

    // ---- legend ----
    QRadioButton *m_legendNoneRadio = nullptr, *m_legendPresetRadio = nullptr, *m_legendCustomRadio = nullptr;
    QWidget *m_legendPresetFrame = nullptr, *m_legendCustomFrame = nullptr;
    QComboBox *m_legendPosCombo = nullptr;
    QLineEdit *m_legendXEdit = nullptr, *m_legendYEdit = nullptr;
    QComboBox *m_legendAnchorCombo = nullptr;

    // ---- curves ----
    struct CurveRow {
        CurveKey yCol;
        QLineEdit *labelEdit;
        ColorPickerWidget *colorPicker;
        QComboBox *markerCombo;
        QComboBox *lineStyleCombo;
        QLineEdit *widthEdit;     // line width (line plots) / outline width (bars, histograms)
        QMap<int, ColorPickerWidget *> fitColorPickers; // slotIdx -> picker
        QMap<int, QLineEdit *> fitLabelEdits;
        QVector<QWidget *> cells;   // the row's widgets, in grid column order (for re-laying out)
        QLineEdit *opacityEdit = nullptr;   // bars / histograms: fill opacity
        QComboBox *fillTypeCombo = nullptr;             // bars / histograms: solid / none / pattern
        ColorPickerWidget *outlineColorPicker = nullptr; // bars / histograms: outline color
        QComboBox *outlineStyleCombo = nullptr;         // bars / histograms: outline line style
        int id = 0;                 // stable id (drag & drop identifies rows by this, not by position)
    };
    QString m_plotType = "line";      // "line" / "bar" / "hist" -- what is plotted in the main window
    QGroupBox *m_barGroup = nullptr;
    QLineEdit *m_barWidthEdit = nullptr;
    QLineEdit *m_xLabelRotEdit = nullptr, *m_yLabelRotEdit = nullptr;               // tick label rotation per axis
    QComboBox *m_xLabelAnchorCombo = nullptr, *m_yLabelAnchorCombo = nullptr;
    QLineEdit *m_y2LabelRotEdit = nullptr;                                           // second axis (only with a second axis)
    QComboBox *m_y2LabelAnchorCombo = nullptr;
    QComboBox *m_histTickCombo = nullptr;         // histogram: ticks at auto / bin edges / bin centres
    bool m_resetRequested = false;

    // color scheme of the curves ("Custom" is the last entry)
    QComboBox *m_schemeCombo = nullptr;
    int m_schemeBusy = 0;                    // > 0 while the program (not the user) is changing colors
    QLineEdit *m_histGapEdit = nullptr;      // histogram: gap between the bars, % of one bin's width
    QLineEdit *m_barGapEdit = nullptr;       // gap between the bars of a group, % of one bar's width

    // axis colors (hidden until switched on)
    QCheckBox *m_axisColorsCheck = nullptr;
    QWidget *m_axisColorsFrame = nullptr;
    // [axis][what]: axis 0 = X, 1 = Y, 2 = second Y; what 0 = line, 1 = ticks, 2 = tick labels, 3 = label, 4 = grid
    ColorPickerWidget *m_axPicker[3][5] = {};
    ColorPickerWidget *m_axTitlePicker = nullptr;
    QCheckBox *m_axY2Check = nullptr;
    // value labels at the bars (bar charts)
    QCheckBox *m_barLblCheck = nullptr, *m_barLblBgCheck = nullptr, *m_barLblBorderCheck = nullptr;
    QWidget *m_barLblFrame = nullptr;
    QComboBox *m_barLblAnchorCombo = nullptr, *m_barLblFontCombo = nullptr;
    ColorPickerWidget *m_barLblTextPicker = nullptr, *m_barLblBgPicker = nullptr, *m_barLblBorderPicker = nullptr;
    QLineEdit *m_barLblOpacityEdit = nullptr, *m_barLblRotEdit = nullptr, *m_barLblXShiftEdit = nullptr,
              *m_barLblYShiftEdit = nullptr, *m_barLblPadEdit = nullptr;
    QCheckBox *m_barLblDecDefaultCheck = nullptr, *m_barLblZeroFillCheck = nullptr;
    QSpinBox *m_barLblDecSpin = nullptr;
    QString settingsGroup() const;                // QSettings group, one per plot type
    QVector<CurveRow> m_curveRows;   // order here == plot order == legend order

    // Rows of the Curve-fit and Integral sections; they follow the curve order.
    struct SectionRow {
        int curveId = 0;
        int fitSlot = -1;                 // fit slot the row belongs to (-1: plain data)
        QVector<QWidget *> cells;
        ColorPickerWidget *picker = nullptr;
    };
    QVector<SectionRow> m_fitRows, m_integralRows;
    QGridLayout *m_fitRowsGrid = nullptr, *m_integralRowsGrid = nullptr;
    void relayoutSectionRows(QGridLayout *grid, QVector<SectionRow> &rows, int stretchCol);
    QGridLayout *m_curveGrid = nullptr;
    QFrame *m_curveDropLine = nullptr;
    void moveCurveRow(int from, int to);
    void relayoutCurveRows();
    int curveRowIndexById(int id) const;
    int curveInsertIndexAt(int yInBox) const;
    void showCurveDropLine(int yInBox);

    // ---- error bars ----
    QGroupBox *m_errGroup = nullptr;
    bool m_errAvailable = false;
    QCheckBox *m_includeErrCheck = nullptr, *m_errSameColorCheck = nullptr;
    QComboBox *m_errDirCombo = nullptr, *m_errStyleCombo = nullptr, *m_errCapCombo = nullptr;
    ColorPickerWidget *m_errColorPicker = nullptr;
    QLineEdit *m_errWidthEdit = nullptr, *m_errCapSizeEdit = nullptr;

    // ---- fit ----
    QCheckBox *m_includeFitCheck = nullptr;
    QLineEdit *m_fitSamplesEdit = nullptr, *m_fitWidthEdit = nullptr;
    QComboBox *m_fitStyleCombo = nullptr;

    // ---- integral ----
    QCheckBox *m_includeIntegralCheck = nullptr;
    QLineEdit *m_integralOpacityEdit = nullptr;
    QCheckBox *m_integralShowLabelCheck = nullptr;
    QMap<IntegralResultKey, ColorPickerWidget *> m_integralColorPickers;

    // ---- figure wrapper ----
    QCheckBox *m_wrapFigureCheck = nullptr;
    QLineEdit *m_captionEdit = nullptr, *m_figLabelEdit = nullptr;
    QComboBox *m_placementCombo = nullptr;
};
