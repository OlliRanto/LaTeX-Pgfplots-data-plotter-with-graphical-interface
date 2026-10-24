#pragma once
#include <QString>
#include <QVector>
#include <QMap>
#include <QPair>
#include "Dataset.h"

struct LatexCurveOpts {
    CurveKey yCol;           // yCol.y2 == true: drawn against the second (right) Y axis
    int dsIdx = 0;
    QString xColName;      // "index" or column name
    // CSV mode: reference columns by position (x index=N / y index=N) instead of by
    // header name. Used for files without a header row (auto-named Col1, Col2, ...)
    // and when X is the row index, where names are meaningless or may collide.
    bool xByIndex = false;
    bool yByIndex = false;
    QString csvFilename;   // used when dataSource == "csv"
    QString csvColsep;     // "comma"/"semicolon"/"tab"/"space"
    QString legendLabel;
    QString color;         // name or hex
    QString marker;
    QString linestyle;
    double linewidthPt = 0.4;
    QVector<QPair<double, double>> points; // used when dataSource == "embed"
    // Error bars (symmetric +/- std-dev on Y)
    double fillOpacity = 1.0;  // bar / histogram fill opacity (linewidthPt is then the outline width)
    // bar / histogram look: fill type ("solid", "none" or a TikZ pattern, drawn in `color`),
    // and the outline's color and style (its width is linewidthPt)
    QString fillType = "solid";
    QString outlineColor = "black";
    QString outlineStyle = "solid";
    bool hasErr = false;
    QString errColName;        // CSV mode: header name of the std-dev column in the source file
    QVector<double> errs;      // embed mode: parallel to `points`
};

// Global look of the error bars (applied to every curve that has them).
struct LatexErrorBarOpts {
    bool enabled = false;
    QString direction = "both";   // both / plus / minus
    QString color;                // empty = same color as the curve
    double linewidthPt = 0.4;
    QString linestyle = "solid";
    QString capMark = "cap";      // cap / none / dot / diamond / square
    double capSizePt = 2.0;       // half-width of the cap / size of the mark
};

struct LatexFitCurveOpts {
    CurveKey yCol;
    int slotIdx = 0;
    QString expression;    // pgfmath, in terms of x
    double domainMin = 0, domainMax = 1;
    QString color;
    QString linestyle = "dashed";
    QString legendLabel;
    int samples = 200;
    double linewidthPt = 0.4;
    bool wasCustomFit = false; // for the translation-caveat comment
};

struct LatexIntegralRegionOpts {
    QString regionLabel;
    QString color;
    double opacity = 0.3;
    bool y2 = false;                       // belongs to the second Y axis
    QVector<QPair<double, double>> coords; // polygon, already closed at y=0
    double area = 0;
    bool showLabel = true;
    double labelX = 0, labelY = 0;
};

struct LatexExtraFunctionOpts {
    QString expression;
    double domainMin = 0, domainMax = 1;
    QString color = "black";
    QString legendLabel;
    int samples = 200;
    double linewidthPt = 0.4;
};

struct LatexCsvExportEntry { QString filename, colsep; };

struct LatexExportOptions {
    // "line" (default), "bar" (grouped bars) or "hist" (overlaid step histogram)
    QString plotType = "line";
    double barWidthPt = 8;                 // bar charts: width of one bar
    bool flipBars = false;                 // bar charts / histograms: horizontal bars (xbar); the "x" data is then the category
    // rotation / anchor of the tick labels along the category axis (bars) or X axis (histogram)
    // tick label rotation / anchor per axis ("default" or a TikZ anchor such as "east")
    bool hasXTickLabelRotate = false, hasYTickLabelRotate = false;
    double xTickLabelRotate = 0, yTickLabelRotate = 0;
    QString xTickLabelAnchor = "default", yTickLabelAnchor = "default";
    bool hasY2TickLabelRotate = false;
    double y2TickLabelRotate = 0;
    QString y2TickLabelAnchor = "default";
    QStringList xTickPositions, xTickLabels;  // bar charts with a text X column
    QString title, xlabel, ylabel;
    // true: add a backslash before LaTeX special characters in titles/labels/legend
    // entries/caption ($ _ & % # { } ~ ^ \). false: write them exactly as typed.
    bool escapeText = false;
    bool hasYlabelRotate = false;
    double ylabelRotate = 0;

    bool hasXlim = false, hasYlim = false;
    double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
    bool logx = false, logy = false;

    // Second Y axis (drawn as a second, overlaid pgfplots axis on the right)
    bool hasSecondAxis = false;
    QString y2label;
    bool hasY2lim = false;
    double y2min = 0, y2max = 0;
    bool logy2 = false;
    bool hasY2tickStep = false;
    double y2tickStep = 0;
    int y2decimals = 2;
    bool y2DefaultDecimals = true;      // true: no precision override on the second axis
    bool hasY2labelRotate = false;
    double y2labelRotate = 0;

    bool hasXtickStep = false, hasYtickStep = false;
    double xtickStep = 0, ytickStep = 0;
    int xdecimals = 2, ydecimals = 2;
    // "Default" = don't emit a precision for that axis; pgfplots picks its own.
    bool xDefaultDecimals = true, yDefaultDecimals = true;
    // minor ticks, set per axis (the dialog fills all three the same when "same for all axes" is ticked)
    bool xMinorTicks = false, yMinorTicks = false, y2MinorTicks = false;
    int xMinorTickNum = 3, yMinorTickNum = 3, y2MinorTickNum = 3;
    QString tickAlign = "outside";
    QString xtickPos = "default", ytickPos = "default";
    bool zeroFill = false;
    bool hideTicks = false;

    QString gridMode = "none";  // none / major / both
    bool axisBox = false;

    QString legendMode = "preset"; // none / preset / custom
    QString legendPos = "north east";
    double legendX = 0.5, legendY = -0.15;
    QString legendAnchor = "north";

    double widthCm = 10, heightCm = 7;
    QString smoothing = "none"; // none / smooth / sharp plot

    QString dataSource = "embed"; // embed / csv
    QMap<int, LatexCsvExportEntry> csvExports; // dsIdx -> entry

    QVector<LatexCurveOpts> curves;
    LatexErrorBarOpts errorBars;
    bool includeFit = false;
    QVector<LatexFitCurveOpts> fitCurves;
    QVector<LatexIntegralRegionOpts> integralRegions;
    bool hasExtraFunction = false;
    LatexExtraFunctionOpts extraFunction;

    bool wrapFigure = false;
    QString caption, figLabel, figurePlacement = "htbp";
};

// Converts a math expression written in our ExprEval dialect into pgfmath
// syntax (natural log -> ln, arcsin -> asin, etc), substituting fitted
// parameter values in place of their names -- mirrors _translate_custom_expr.
QString translateCustomExpr(const QString &expr, const QStringList &paramNames,
                             const QVector<double> &popt);

// Builds a complete, standalone, copy-paste-ready LaTeX document containing
// a pgfplots axis -- mirrors build_pgfplots_code() in the Python app.
QString buildLatexDocument(const LatexExportOptions &opts);

// Builds one CSV file per distinct source dataset used by the exported
// curves (only relevant when dataSource == "csv") -- mirrors
// build_csv_export_text(). Returns {filename: csv text}.
QMap<QString, QString> buildCsvExportText(const LatexExportOptions &opts,
                                           const QVector<Dataset> &datasets);
