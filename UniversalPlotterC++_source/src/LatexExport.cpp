#include "LatexExport.h"
#include "LatexColors.h"
#include <QRegularExpression>
#include <QStringList>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QString fmt6(double v) { return QString::number(v, 'g', 6); }
QString fmt10(double v) { return QString::number(v, 'g', 10); }
QString fmt2(double v) { return QString::number(v, 'g', 2); }
QString fmt4(double v) { return QString::number(v, 'g', 4); }

const QMap<QString, QString> &csvSepChars() {
    static const QMap<QString, QString> m = { {"comma", ","}, {"semicolon", ";"}, {"tab", "\t"}, {"space", " "} };
    return m;
}

// Distinct Y column names exported for one dataset, in first-appearance order.
// buildCsvExportText() writes them right after the X column, so a Y column's
// position in the file is 1 + its position in this list.
QStringList csvYColumns(const LatexExportOptions &opts, int dsIdx) {
    QStringList yCols;
    for (const auto &c : opts.curves)
        if (c.dsIdx == dsIdx && !yCols.contains(c.yCol.col)) yCols << c.yCol.col;
    return yCols;
}

// Distinct std-dev column names exported for one dataset, in first-appearance order.
// They are written after the Y columns, so std-dev column k sits at position
// 1 + csvYColumns().size() + k in the exported file.
QStringList csvErrNames(const LatexExportOptions &opts, int dsIdx) {
    QStringList names;
    if (!opts.errorBars.enabled) return names;
    for (const auto &c : opts.curves)
        if (c.dsIdx == dsIdx && c.hasErr && !c.errColName.isEmpty() && !names.contains(c.errColName))
            names << c.errColName;
    return names;
}

// Header used for each std-dev column in the CSV: the original name, made unique so it
// can't shadow the X / Y headers (or another std-dev header) of the same file.
QStringList csvErrHeaders(const QStringList &errNames, const QString &xName, const QStringList &yCols) {
    QStringList headers;
    for (const QString &n : errNames) {
        QString h = n;
        while (h == xName || yCols.contains(h) || headers.contains(h)) h += "_err";
        headers << h;
    }
    return headers;
}
} // namespace

QString translateCustomExpr(const QString &exprIn, const QStringList &paramNames, const QVector<double> &popt) {
    QString out = exprIn;

    QVector<int> order(paramNames.size());
    for (int i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return paramNames[a].size() > paramNames[b].size(); });
    for (int idx : order) {
        QRegularExpression re("\\b" + QRegularExpression::escape(paramNames[idx]) + "\\b");
        out.replace(re, QString("(%1)").arg(fmt10(popt[idx])));
    }

    out.replace(QRegularExpression("np\\."), "");
    out.replace(QRegularExpression("\\barcsin\\b"), "asin");
    out.replace(QRegularExpression("\\barccos\\b"), "acos");
    out.replace(QRegularExpression("\\barctan\\b"), "atan");
    out.replace(QRegularExpression("\\blog2\\b"), "(ln(x)/ln(2))");
    out.replace(QRegularExpression("\\blog\\b"), "ln");
    out.replace(QRegularExpression("\\bpower\\("), "pow(");
    return out;
}

QString functionToPgf(const QString &exprIn) {
    QString out = exprIn;
    // log2(a) -> (ln(a)/ln(2)): needs the matching closing parenthesis of its argument
    for (;;) {
        QRegularExpressionMatch m = QRegularExpression("\\blog2\\s*\\(").match(out);
        if (!m.hasMatch()) break;
        int open = m.capturedEnd() - 1, depth = 0, close = -1;
        for (int i = open; i < out.size(); ++i) {
            if (out[i] == '(') ++depth;
            else if (out[i] == ')' && --depth == 0) { close = i; break; }
        }
        if (close < 0) break;   // unbalanced: leave it, LaTeX will complain
        QString arg = out.mid(open + 1, close - open - 1);
        out.replace(m.capturedStart(), close - m.capturedStart() + 1, "(ln(" + arg + ")/ln(2))");
    }
    out.replace(QRegularExpression("\\blog\\b(?=\\s*\\()"), "ln");   // natural log; log10 is left alone
    return out;
}

QMap<QString, QString> buildCsvExportText(const LatexExportOptions &opts, const QVector<Dataset> &datasets) {
    QMap<int, QVector<const LatexCurveOpts *>> curvesByDs;
    for (const auto &c : opts.curves) curvesByDs[c.dsIdx].push_back(&c);

    QMap<QString, QString> files;
    for (auto it = curvesByDs.constBegin(); it != curvesByDs.constEnd(); ++it) {
        int dsIdx = it.key();
        if (!opts.csvExports.contains(dsIdx)) continue;
        if (dsIdx < 0 || dsIdx >= datasets.size()) continue;
        const Dataset &ds = datasets[dsIdx];
        const LatexCsvExportEntry &exp = opts.csvExports[dsIdx];

        QString xColName = it.value().first()->xColName;
        QVector<double> xSeries;
        int n = ds.columns.isEmpty() ? 0 : ds.columns.first().values.size();
        if (xColName != "index") {
            int ci = ds.columnIndex(xColName);
            xSeries = (ci >= 0) ? ds.columns[ci].values : QVector<double>(n, std::numeric_limits<double>::quiet_NaN());
        } else {
            xSeries.resize(n);
            for (int i = 0; i < n; ++i) xSeries[i] = i;
        }

        QStringList yCols = csvYColumns(opts, dsIdx);

        QVector<int> yColIdx;
        for (const QString &yc : yCols) yColIdx << ds.columnIndex(yc);

        QStringList errNames = csvErrNames(opts, dsIdx);
        QStringList errHeaders = csvErrHeaders(errNames, xColName, yCols);
        QVector<int> errCis;
        for (const QString &en : errNames) errCis << ds.columnIndex(en);

        QString sep = csvSepChars().value(exp.colsep, ",");
        QString text = xColName + sep + yCols.join(sep);
        for (const QString &h : errHeaders) text += sep + h;
        text += "\n";
        for (int row = 0; row < n; ++row) {
            if (row >= xSeries.size() || std::isnan(xSeries[row])) continue;
            bool anyNan = false;
            QStringList rowVals;
            rowVals << QString::number(xSeries[row], 'g', 12);
            for (int ci : yColIdx) {
                double v = (ci >= 0 && row < ds.columns[ci].values.size()) ? ds.columns[ci].values[row] : std::numeric_limits<double>::quiet_NaN();
                if (std::isnan(v)) { anyNan = true; break; }
                rowVals << QString::number(v, 'g', 12);
            }
            if (anyNan) continue;
            for (int errCi : errCis) {
                double e = (errCi >= 0 && row < ds.columns[errCi].values.size()) ? ds.columns[errCi].values[row]
                                                                                 : std::numeric_limits<double>::quiet_NaN();
                rowVals << QString::number(std::isnan(e) ? 0.0 : std::fabs(e), 'g', 12);
            }
            text += rowVals.join(sep) + "\n";
        }
        files[exp.filename] = text;
    }
    return files;
}

QString buildLatexDocument(const LatexExportOptions &opts) {
    auto esc = [&opts](const QString &t) { return opts.escapeText ? texEscape(t) : t; };
    QStringList lines;
    lines << "% Generated by Universal Data Plotter -- LaTeX (pgfplots) export"
          << "% Paste the tikzpicture into your document, or compile this file as-is.";

    bool wrapFigure = opts.wrapFigure;
    if (wrapFigure) {
        lines << "\\documentclass{article}"
              << "\\usepackage[margin=1in]{geometry}";
        // the [H] ("put it exactly here") placement comes from the float package
        if (opts.figurePlacement.contains('H')) lines << "\\usepackage{float}";
    } else {
        lines << "\\documentclass[border=2pt]{standalone}";
    }
    lines << "\\usepackage{pgfplots}"
          << "\\pgfplotsset{compat=1.18}";
    if ((opts.plotType == "bar" || opts.plotType == "hist")
        && std::any_of(opts.curves.begin(), opts.curves.end(), [](const LatexCurveOpts &c) { return latexFillIsPattern(c.fillType); }))
        lines << "\\usetikzlibrary{patterns}";   // striped / dotted ... bar fills
    lines << "";

    QStringList colorDefs;
    QSet<QString> seenHex;
    QVector<QString> colorSources;
    for (const auto &c : opts.curves) colorSources << c.color;
    if (opts.plotType == "bar" || opts.plotType == "hist")
        for (const auto &c : opts.curves) colorSources << c.outlineColor;
    for (const auto &c : opts.fitCurves) colorSources << c.color;
    for (const auto &c : opts.integralRegions) colorSources << c.color;
    if (opts.errorBars.enabled && !opts.errorBars.color.isEmpty()) colorSources << opts.errorBars.color;
    if (opts.axisColors.enabled) {
        const auto &ac = opts.axisColors;
        for (const LatexAxisColorSet *set : {&ac.x, &ac.y})
            colorSources << set->line << set->tick << set->tickLabel << set->label << set->grid;
        colorSources << ac.title;
        if (ac.hasY2) colorSources << ac.y2.line << ac.y2.tick << ac.y2.tickLabel << ac.y2.label << ac.y2.grid;
    }
    if (opts.plotType == "bar" && opts.barLabels.enabled) {
        colorSources << opts.barLabels.textColor;
        if (opts.barLabels.hasBackground) colorSources << opts.barLabels.backgroundColor;
        if (opts.barLabels.hasBorder) colorSources << opts.barLabels.borderColor;
    }
    for (const QString &cvRaw : colorSources) {
        QString cv = cvRaw.trimmed();
        if (isHexColor(cv) && !seenHex.contains(cv.toUpper())) {
            colorDefs << QString("\\definecolor{%1}{HTML}{%2}").arg(pgfColorName(cv.toUpper()), cv.toUpper());
            seenHex.insert(cv.toUpper());
        }
    }
    if (!colorDefs.isEmpty()) { lines += colorDefs; lines << ""; }

    lines << "\\begin{document}";
    if (wrapFigure) {
        lines << QString("\\begin{figure}[%1]").arg(opts.figurePlacement)
              << "    \\centering";
    }
    const bool isBar = opts.plotType == "bar";
    const bool isHist = opts.plotType == "hist";
    const bool flip = (isBar || isHist) && opts.flipBars;   // horizontal bars / bins: values along x, categories along y
    const bool twoAxes = opts.hasSecondAxis;
    const bool anyY2Curve = std::any_of(opts.curves.begin(), opts.curves.end(), [](const LatexCurveOpts &c) { return c.yCol.y2; });
    const bool dualAxis = twoAxes && anyY2Curve;

    lines << "\\begin{tikzpicture}";

    // ---- shared axis header pieces ----
    auto sizeLine = [&]() { return QString("    width=%1cm, height=%2cm,").arg(fmt6(opts.widthCm), fmt6(opts.heightCm)); };

    // ================= primary axis =================
    lines << "\\begin{axis}["
          << sizeLine();
    // (sideways bars with a top axis: the title goes on that axis, otherwise it would sit on top of it)
    if (!opts.title.isEmpty() && !(flip && dualAxis)) lines << QString("    title={%1},").arg(esc(opts.title));
    if (!opts.xlabel.isEmpty()) lines << QString("    xlabel={%1},").arg(esc(opts.xlabel));
    if (!opts.ylabel.isEmpty()) lines << QString("    ylabel={%1},").arg(esc(opts.ylabel));
    if (opts.hasYlabelRotate) lines << QString("    ylabel style={rotate=%1},").arg(fmt6(opts.ylabelRotate));
    if (opts.hasXlim) lines << QString("    xmin=%1, xmax=%2,").arg(fmt6(opts.xmin), fmt6(opts.xmax));
    if (opts.hasYlim) lines << QString("    ymin=%1, ymax=%2,").arg(fmt6(opts.ymin), fmt6(opts.ymax));
    if (opts.logx) lines << "    xmode=log,";
    if (opts.logy) lines << "    ymode=log,";
    if (isBar) lines << QString("    %1, bar width=%2pt,").arg(flip ? "xbar" : "ybar", fmt2(opts.barWidthPt));
    if (isBar || isHist) lines << "    area legend,";

    // category axis: where bars sit (x normally, y for sideways bars)
    const QString catAx = flip ? "y" : "x";
    const bool catTicks = !opts.xTickPositions.isEmpty();
    if (opts.hideTicks) {
        lines << "    xtick=\\empty, ytick=\\empty,";
    } else {
        if (catTicks) {
            lines << QString("    %1tick={%2},").arg(catAx, opts.xTickPositions.join(","));
            if (!opts.xTickLabels.isEmpty()) {
                QStringList lbls;
                for (const QString &l : opts.xTickLabels) lbls << QString("{%1}").arg(esc(l));
                lines << QString("    %1ticklabels={%2},").arg(catAx, lbls.join(","));
            }
        }
        if (opts.hasXtickStep && !(catTicks && !flip)) lines << QString("    xtick distance=%1,").arg(fmt6(opts.xtickStep));
        if (opts.hasYtickStep && !(catTicks && flip)) lines << QString("    ytick distance=%1,").arg(fmt6(opts.ytickStep));
        if (opts.xMinorTicks) lines << QString("    minor x tick num=%1,").arg(opts.xMinorTickNum);
        if (opts.yMinorTicks) lines << QString("    minor y tick num=%1,").arg(opts.yMinorTickNum);
        // An axis set to "default" gets no number-format override at all, so
        // zero-fill can only apply to axes with an explicit number of decimals.
        // One combined "tick label style" per axis: number format + rotation / anchor
        // (a second assignment of the same key would replace the first).
        QString zf = opts.zeroFill ? ",fixed zerofill" : "";
        auto labelLook = [&](bool isX) {
            QStringList look;
            const bool hasRot = isX ? opts.hasXTickLabelRotate : opts.hasYTickLabelRotate;
            const double rot = isX ? opts.xTickLabelRotate : opts.yTickLabelRotate;
            const QString &anchor = isX ? opts.xTickLabelAnchor : opts.yTickLabelAnchor;
            if (hasRot) look << QString("rotate=%1").arg(fmt6(rot));
            if (anchor != "default") look << QString("anchor=%1").arg(anchor);
            return look;
        };
        auto tickLabelStyle = [&](const QString &axis, bool defaultDecimals, int decimals) {
            QStringList parts;
            if (!defaultDecimals)
                parts << QString("/pgf/number format/.cd,fixed%1,precision=%2,/tikz/.cd").arg(zf).arg(decimals);
            parts << labelLook(axis == "x");
            if (!parts.isEmpty()) lines << QString("    %1 tick label style={%2},").arg(axis, parts.join(","));
        };
        tickLabelStyle("x", opts.xDefaultDecimals, opts.xdecimals);
        tickLabelStyle("y", opts.yDefaultDecimals, opts.ydecimals);
        lines << QString("    tick align=%1,").arg(opts.tickAlign);
    }
    // Box + second axis: the box is drawn by the first axis, the second axis adds its own ticks and
    // numbers on the opposite side. So the first axis may only tick its own side of the value axis
    // (left, or bottom for sideways bars); the user's tick position for that axis is overridden.
    const bool boxDual = dualAxis && opts.axisBox;
    if (opts.xtickPos != "default" && !(boxDual && flip)) lines << QString("    xtick pos=%1,").arg(opts.xtickPos);
    if (opts.ytickPos != "default" && !(boxDual && !flip)) lines << QString("    ytick pos=%1,").arg(opts.ytickPos);
    if (boxDual) lines << (flip ? "    xtick pos=bottom," : "    ytick pos=left,");

    if (opts.gridMode != "none") lines << QString("    grid=%1,").arg(opts.gridMode);

    bool hasSeries = !opts.curves.isEmpty() || !opts.fitCurves.isEmpty();
    auto emitLegendOptions = [&]() {
        if (!(hasSeries && opts.legendMode != "none")) return;
        if (opts.legendMode == "preset") {
            lines << QString("    legend pos=%1,").arg(opts.legendPos);
        } else if (opts.legendMode == "custom") {
            lines << QString("    legend style={at={(%1,%2)},anchor=%3},")
                         .arg(fmt6(opts.legendX), fmt6(opts.legendY), opts.legendAnchor);
        }
        lines << "    legend cell align=left,";
    };
    // With two axes the legend lives in the second one: it is drawn last, so it is never
    // covered by the second axis' curves or bars.
    if (!dualAxis) emitLegendOptions();

    if (dualAxis && !opts.axisBox) {
        // open axes: left + bottom here, the right-hand (or top) axis is drawn by the second axis below
        lines << "    axis x line*=bottom, axis y line*=left,";
    } else {
        // (with a second axis and a box, the second axis is drawn over the box' right / top edge)
        lines << (opts.axisBox ? "    axis lines=box," : "    axis lines=left,");
    }
    // Axis colors (only when switched on). X, Y and the second axis each have their own set.
    // "which" is the pgfplots axis letter ("x" or "y") the set is written for.
    auto axisColorLines = [&](bool second) {
        QStringList out;
        if (!opts.axisColors.enabled) return out;
        const auto &ac = opts.axisColors;
        auto col = [&](const QString &token) { return resolveColorToken(token.isEmpty() ? QString("black") : token); };
        auto emitSet = [&](const QString &ax, const LatexAxisColorSet &c, bool withGrid) {
            out << QString("    %1 axis line style={color=%2},").arg(ax, col(c.line))
                << QString("    every %1 tick/.append style={color=%2},").arg(ax, col(c.tick))
                << QString("    every %1 tick label/.append style={color=%2},").arg(ax, col(c.tickLabel))
                << QString("    every axis %1 label/.append style={color=%2},").arg(ax, col(c.label));
            if (withGrid) out << QString("    every %1 grid/.append style={color=%2},").arg(ax, col(c.grid));
        };
        if (!second) {
            emitSet("x", ac.x, true);
            emitSet("y", ac.y, true);
            out << QString("    every axis title/.append style={color=%1},").arg(col(ac.title));
        } else {
            // the second axis is the value axis of the overlaid axis: y normally, x for sideways bars
            emitSet(flip ? "x" : "y", ac.hasY2 ? ac.y2 : ac.y, false);
            out << QString("    every axis title/.append style={color=%1},").arg(col(ac.title));
        }
        return out;
    };
    // Value labels at the bars (bar charts only)
    auto barLabelLines = [&]() {
        QStringList out;
        if (!isBar || !opts.barLabels.enabled) return out;
        const auto &bl = opts.barLabels;
        auto col = [&](const QString &token) { return resolveColorToken(token.isEmpty() ? QString("black") : token); };
        QStringList st;
        if (bl.anchor != "default") st << QString("anchor=%1").arg(bl.anchor);
        st << QString("text=%1").arg(col(bl.textColor));
        if (bl.hasBackground) st << QString("fill=%1").arg(col(bl.backgroundColor))
                                 << QString("fill opacity=%1").arg(fmt2(bl.backgroundOpacity));
        if (bl.hasBorder) st << QString("draw=%1").arg(col(bl.borderColor));
        if (bl.fontSize != "default") st << QString("font=\\%1").arg(bl.fontSize);
        if (bl.hasRotate) st << QString("rotate=%1").arg(fmt6(bl.rotate));
        if (bl.hasBackground || bl.hasBorder) st << QString("inner sep=%1pt").arg(fmt2(bl.innerSepPt));
        if (bl.xshiftPt != 0) st << QString("xshift=%1pt").arg(fmt2(bl.xshiftPt));
        if (bl.yshiftPt != 0) st << QString("yshift=%1pt").arg(fmt2(bl.yshiftPt));
        if (!bl.defaultDecimals) {
            QString zf = opts.zeroFill ? ",fixed zerofill" : "";
            st << QString("/pgf/number format/.cd,fixed%1,precision=%2,/tikz/.cd").arg(zf).arg(bl.decimals);
        }
        out << "    nodes near coords,"
            << QString("    nodes near coords style={%1},").arg(st.join(","));
        return out;
    };
    for (const QString &l : barLabelLines()) lines << l;
    for (const QString &l : axisColorLines(false)) lines << l;

    lines << "]"
          << "";

    // ---- emitters shared by both axes ----
    auto emitIntegrals = [&](bool wantY2) {
        bool header = false;
        for (const auto &reg : opts.integralRegions) {
            if (reg.y2 != wantY2) continue;
            if (!header) { lines << "% -- integral / area-under-curve shading --"; header = true; }
            QStringList coordStrs;
            for (const auto &p : reg.coords) coordStrs << QString("(%1,%2)").arg(fmt6(p.first), fmt6(p.second));
            lines << QString("\\addplot[fill=%1, draw=none, opacity=%2, forget plot]")
                         .arg(resolveColorToken(reg.color), fmt4(reg.opacity));
            lines << "    coordinates {"
                  << QString("        %1").arg(coordStrs.join(" "))
                  << "    } \\closedcycle;";
            if (reg.showLabel) {
                lines << QString("\\node at (axis cs:%1,%2) [font=\\footnotesize] {$A \\approx %3$};")
                             .arg(fmt6(reg.labelX), fmt6(reg.labelY), fmt6(reg.area));
            }
            lines << "";
        }
    };

    // Style options shared by a curve's \addplot and (for second-axis curves) the
    // \addlegendimage that stands in for it in the primary axis' legend.
    // Bar / histogram look: filled shape with an outline
    auto fillBits = [&](const LatexCurveOpts &c) {
        QStringList b;
        if (c.fillType == "none") {
            b << "fill=none";
        } else if (latexFillIsPattern(c.fillType)) {
            // patterns are drawn in the series color; they do not take a fill opacity
            b << QString("pattern=%1").arg(c.fillType) << QString("pattern color=%1").arg(resolveColorToken(c.color));
        } else {
            b << QString("fill=%1").arg(resolveColorToken(c.color));
            if (c.fillOpacity < 0.999) b << QString("fill opacity=%1").arg(fmt4(c.fillOpacity));
        }
        if (c.linewidthPt > 0 && c.outlineStyle != "none") {
            QString oc = c.outlineColor.trimmed().isEmpty() ? QString("black") : resolveColorToken(c.outlineColor);
            b << QString("draw=%1").arg(oc) << QString("line width=%1pt").arg(fmt2(c.linewidthPt));
            if (c.outlineStyle != "solid" && !c.outlineStyle.isEmpty()) b << c.outlineStyle;
        } else {
            b << "draw=none";
        }
        return b;
    };
    // ---- stacked bars ----
    // Every curve belongs to a "slot" of the bar group; a stack (a chain of curves, each on top of the
    // previous one) is one slot. emitOrder lists the curves so that every stack is contiguous, base first.
    QVector<int> slotOf(opts.curves.size(), 0), emitOrder;
    QVector<bool> inStack(opts.curves.size(), false);
    QVector<int> stackRootOf(opts.curves.size(), -1);   // index of the lowest curve of the stack
    int slotCount = 0;
    {
        const int n = opts.curves.size();
        QVector<int> baseIdx(n, -1), topIdx(n, -1);
        for (int i = 0; i < n && isBar; ++i) {
            const auto &c = opts.curves[i];
            if (!c.hasStackOn) continue;
            for (int j = 0; j < n; ++j)
                if (j != i && opts.curves[j].yCol == c.stackOn && opts.curves[j].yCol.y2 == c.yCol.y2 && topIdx[j] < 0) {
                    baseIdx[i] = j; topIdx[j] = i; break;
                }
        }
        QVector<bool> done(n, false);
        for (int i = 0; i < n; ++i) {
            if (done[i]) continue;
            int root = i, guard = 0;
            while (baseIdx[root] >= 0 && guard++ <= n) root = baseIdx[root];
            if (done[root]) continue;       // (cannot happen for a chain; guards against a cycle)
            const bool stacked = topIdx[root] >= 0;
            for (int cur = root, g = 0; cur >= 0 && !done[cur] && g <= n; cur = topIdx[cur], ++g) {
                done[cur] = true;
                emitOrder << cur;
                slotOf[cur] = slotCount;
                inStack[cur] = stacked;
                stackRootOf[cur] = root;
            }
            ++slotCount;
        }
        for (int i = 0; i < n; ++i) if (!done[i]) { emitOrder << i; slotOf[i] = slotCount++; }
    }
    if (slotCount == 0) slotCount = 1;

    auto curveStyleBits = [&](const LatexCurveOpts &c) {
        QStringList styleBits;
        if (isBar || isHist) {
            // all bar series share the axis, side by side: explicit shift per series
            if (isBar) {
                const int ci = int(&c - opts.curves.constData());
                const int idx = slotOf[ci];
                const int K = slotCount;
                // neighbouring bars of a group are one bar width + the gap apart (a whole stack is one bar)
                const double step = opts.barWidthPt * (1.0 + opts.barGap);
                styleBits << QString("bar shift=%1pt").arg(fmt2((idx - (K - 1) / 2.0) * step));
                if (inStack[ci]) styleBits << QString("stack plots=%1").arg(flip ? "x" : "y");
            } else {
                // (the sideways outline is written out as a polygon below; with a gap every bar is its own polygon)
                if (!flip && opts.histGap <= 1e-9) styleBits << "const plot";
            }
            styleBits << fillBits(c);
            return styleBits;
        }
        styleBits << QString("color=%1").arg(resolveColorToken(c.color));
        if (c.marker != "none") {
            styleBits << (c.marker.contains(' ') ? QString("mark={%1}").arg(c.marker) : QString("mark=%1").arg(c.marker));
        } else {
            styleBits << "no markers";
        }
        if (c.linestyle == "none") styleBits << "only marks";
        else if (c.linestyle != "solid") styleBits << c.linestyle;
        styleBits << QString("line width=%1pt").arg(fmt2(c.linewidthPt));
        return styleBits;
    };

    QString prevCsvFilename = "\u0001__none__";
    auto emitCurves = [&](bool wantY2, bool withLegend, bool forget = false) {
        int prevStackRoot = -1;                 // the stack drawn last on this axis
        for (int ci : emitOrder) {
            const auto &c = opts.curves[ci];
            if (c.yCol.y2 != wantY2) continue;
            if (isBar && inStack[ci] && stackRootOf[ci] != prevStackRoot) {
                // pgfplots has one running total per axis: a new stack starts from zero again only after an
                // invisible bar that cancels the total of the previous stack.
                if (prevStackRoot >= 0) {
                    QMap<double, double> total;
                    for (int j = 0; j < opts.curves.size(); ++j)
                        if (inStack[j] && stackRootOf[j] == prevStackRoot)
                            for (const auto &p : opts.curves[j].points) total[p.first] += p.second;
                    QStringList cs;
                    for (auto it = total.constBegin(); it != total.constEnd(); ++it)
                        cs << (flip ? QString("(%1,%2)").arg(fmt6(-it.value()), fmt6(it.key()))
                                    : QString("(%1,%2)").arg(fmt6(it.key()), fmt6(-it.value())));
                    lines << "% -- invisible bar that ends the previous stack --"
                          << QString("\\addplot[forget plot, stack plots=%1, draw=none, fill=none, bar shift=%2pt, "
                                    "every node near coord/.append style={opacity=0}]")
                                 .arg(flip ? "x" : "y")
                                 .arg(fmt2((slotOf[ci] - (slotCount - 1) / 2.0) * opts.barWidthPt * (1.0 + opts.barGap)))
                          << "    coordinates {" << QString("        %1").arg(cs.join(" ")) << "    };" << "";
                }
                prevStackRoot = stackRootOf[ci];
            }
            if (c.isFunction) {
                // drawn analytically, like the curve fits: no data points involved
                QStringList fb = curveStyleBits(c);
                if (forget) fb << "forget plot";
                fb << QString("domain=%1:%2").arg(fmt6(c.funcMin), fmt6(c.funcMax))
                   << QString("samples=%1").arg(c.funcSamples)
                   << "trig format plots=rad";   // sin / cos / ... take radians, as in the app
                // values far outside the axis range would overflow TeX: cut them (a gap, e.g. at tan's poles)
                const bool lim = wantY2 ? opts.hasY2lim : opts.hasYlim;
                if (lim) {
                    double lo = wantY2 ? opts.y2min : opts.ymin, hi = wantY2 ? opts.y2max : opts.ymax;
                    double span = std::max(hi - lo, 1e-12);
                    fb << QString("restrict y to domain=%1:%2").arg(fmt6(lo - 10 * span), fmt6(hi + 10 * span))
                       << "unbounded coords=jump";
                }
                lines << QString("\\addplot[%1]").arg(fb.join(", "))
                      << QString("    {%1};").arg(functionToPgf(c.funcExpr));
                if (withLegend && opts.legendMode != "none") lines << QString("\\addlegendentry{%1}").arg(esc(c.legendLabel));
                lines << "";
                continue;
            }
            if (opts.dataSource == "csv" && c.csvFilename != prevCsvFilename) {
                lines << QString("%% -- data from %1 --").arg(c.csvFilename);
                prevCsvFilename = c.csvFilename;
            }
            QStringList styleBits = curveStyleBits(c);
            if (opts.smoothing != "none" && !isBar && !isHist) styleBits << opts.smoothing;
            if (forget) styleBits << "forget plot";   // legend entry is written separately, in the user's order

            bool curveErr = opts.errorBars.enabled && c.hasErr;
            if (curveErr) {
                // Error-bar keys live in the error bars/ path, so they must come last.
                const LatexErrorBarOpts &eb = opts.errorBars;
                QString ebColor = eb.color.isEmpty() ? resolveColorToken(c.color) : resolveColorToken(eb.color);
                QStringList ebStyle;
                ebStyle << QString("color=%1").arg(ebColor) << QString("line width=%1pt").arg(fmt2(eb.linewidthPt));
                if (eb.linestyle != "solid" && eb.linestyle != "none") ebStyle << eb.linestyle;
                const QString ed = flip ? "x" : "y";   // error bars run along the value axis
                styleBits << "error bars/.cd" << QString("%1 dir=%2").arg(ed, eb.direction) << QString("%1 explicit").arg(ed)
                          << QString("error bar style={%1}").arg(ebStyle.join(", "));
                if (eb.capMark == "none") {
                    styleBits << "error mark=none";
                } else if (eb.capMark == "cap") {
                    styleBits << QString("error mark options={%1mark size=%2pt, color=%3}")
                                     .arg("rotate=90, ", fmt2(eb.capSizePt), ebColor);   // the default mark is turned 90 deg for both orientations
                } else {
                    QString m = eb.capMark == "dot" ? "*" : eb.capMark == "diamond" ? "diamond*" : "square*";
                    styleBits << QString("error mark=%1").arg(m)
                              << QString("error mark options={mark size=%1pt, color=%2}")
                                     .arg(fmt2(eb.capSizePt), ebColor);
                }
            }

            // a stacked bar's label should show the bar's own value, not the running total pgfplots would show
            const bool ownMeta = isBar && inStack[ci] && opts.barLabels.enabled && opts.dataSource != "csv";
            if (ownMeta) styleBits.insert(styleBits.size() > 0 ? 1 : 0, "point meta=explicit");
            lines << QString("\\addplot[%1]").arg(styleBits.join(", "));
            if (opts.dataSource == "csv") {
                // The exported CSV is [X, Y1, Y2, ...] per source file. Columns without a
                // real header name (headerless files, row-index X) are addressed by
                // position; so is a Y column that shares its name with the X column.
                QStringList yCols = csvYColumns(opts, c.dsIdx);
                int yPos = 1 + std::max(0, int(yCols.indexOf(c.yCol.col)));
                bool xIdx = c.xByIndex || c.xColName == "index";
                bool yIdx = c.yByIndex || c.yCol.col == c.xColName;
                QString xSel = xIdx ? QString("x index=0") : QString("x={%1}").arg(c.xColName);
                QString ySel = yIdx ? QString("y index=%1").arg(yPos) : QString("y={%1}").arg(c.yCol.col);
                QString errSel;
                if (curveErr && !c.errColName.isEmpty()) {
                    QStringList errNames = csvErrNames(opts, c.dsIdx);
                    QStringList errHeaders = csvErrHeaders(errNames, c.xColName, yCols);
                    int k = errNames.indexOf(c.errColName);
                    if (k >= 0) {
                        errSel = c.yByIndex ? QString(", y error index=%1").arg(1 + yCols.size() + k)
                                            : QString(", y error={%1}").arg(errHeaders[k]);
                    }
                }
                if (flip) {
                    // sideways bars: the value column feeds x, the category column feeds y
                    QString catSel = "y" + xSel.mid(1), valSel = "x" + ySel.mid(1);
                    errSel.replace(", y error", ", x error");
                    xSel = valSel; ySel = catSel;
                }
                lines << QString("    table[%1, %2%3, col sep=%4] {%5};")
                             .arg(xSel, ySel, errSel, c.csvColsep, c.csvFilename);
            } else {
                QStringList coordStrs;
                if (isHist && opts.histGap > 1e-9) {
                    // gap between the bars: every bin is a rectangle of its own (a single outline cannot have gaps).
                    // Only the first rectangle makes a legend entry; the others are "forget plot".
                    // c.points holds (edge_i, height_i) with one extra closing point.
                    const int nb = int(c.points.size()) - 1;
                    for (int bi = 0; bi < nb; ++bi) {
                        const double e0 = c.points[bi].first, e1 = c.points[bi + 1].first, h = c.points[bi].second;
                        const double shrink = 0.5 * opts.histGap * (e1 - e0);
                        const double l = e0 + shrink, r = e1 - shrink;
                        if (bi > 0) lines << QString("\\addplot[%1, forget plot]").arg(styleBits.join(", "));
                        const QString rect = flip
                            ? QString("(0,%1) (%2,%1) (%2,%3) (0,%3)").arg(fmt6(l), fmt6(h), fmt6(r))
                            : QString("(%1,0) (%1,%2) (%3,%2) (%3,0)").arg(fmt6(l), fmt6(h), fmt6(r));
                        lines << "    coordinates {" << QString("        %1").arg(rect) << "    } -- cycle;";
                    }
                    if (withLegend && opts.legendMode != "none") lines << QString("\\addlegendentry{%1}").arg(esc(c.legendLabel));
                    lines << "";
                    continue;
                }
                if (isHist && flip) {
                    // sideways histogram: outline of the bins, counts along x, bin edges up y.
                    // c.points holds (edge_i, height_i) with one extra closing point.
                    const int nb = int(c.points.size()) - 1;
                    if (nb > 0) coordStrs << QString("(0,%1)").arg(fmt6(c.points[0].first));
                    for (int bi = 0; bi < nb; ++bi) {
                        coordStrs << QString("(%1,%2)").arg(fmt6(c.points[bi].second), fmt6(c.points[bi].first))
                                  << QString("(%1,%2)").arg(fmt6(c.points[bi].second), fmt6(c.points[bi + 1].first));
                    }
                    if (nb > 0) coordStrs << QString("(0,%1)").arg(fmt6(c.points[nb].first));
                    lines << "    coordinates {"
                          << QString("        %1").arg(coordStrs.join(" "))
                          << "    } -- cycle;";
                    if (withLegend && opts.legendMode != "none") lines << QString("\\addlegendentry{%1}").arg(esc(c.legendLabel));
                    lines << "";
                    continue;
                }
                for (int pi = 0; pi < c.points.size(); ++pi) {
                    const auto &p = c.points[pi];
                    QString pt = flip ? QString("(%1,%2)").arg(fmt6(p.second), fmt6(p.first))
                                      : QString("(%1,%2)").arg(fmt6(p.first), fmt6(p.second));
                    if (curveErr && pi < c.errs.size())
                        pt += flip ? QString(" +- (%1,0)").arg(fmt6(c.errs[pi]))
                                   : QString(" +- (0,%1)").arg(fmt6(c.errs[pi]));
                    if (ownMeta) pt += QString(" [%1]").arg(fmt6(p.second));
                    coordStrs << pt;
                }
                lines << "    coordinates {"
                      << QString("        %1").arg(coordStrs.join(" "))
                      << (isHist ? "    } \\closedcycle;" : "    };");
            }
            if (withLegend && opts.legendMode != "none") lines << QString("\\addlegendentry{%1}").arg(esc(c.legendLabel));
            lines << "";
        }
    };

    auto fitStyleBits = [&](const LatexFitCurveOpts &fc) {
        QStringList styleBits;
        styleBits << QString("color=%1").arg(resolveColorToken(fc.color)) << "no markers";
        if (fc.linestyle == "none") styleBits << "only marks";
        else if (fc.linestyle != "solid") styleBits << fc.linestyle;
        styleBits << QString("line width=%1pt").arg(fmt2(fc.linewidthPt));
        return styleBits;
    };
    bool fitHeader = false;
    auto emitFits = [&](bool wantY2, bool withLegend, bool forget = false) {
        for (const auto &fc : opts.fitCurves) {
            if (fc.yCol.y2 != wantY2) continue;
            if (!fitHeader) { lines << "% -- curve fit(s) --"; fitHeader = true; }
            QStringList fitBits = fitStyleBits(fc);
            if (forget) fitBits << "forget plot";
            lines << QString("\\addplot[%1, domain=%2:%3, samples=%4]")
                         .arg(fitBits.join(", "), fmt6(fc.domainMin), fmt6(fc.domainMax)).arg(fc.samples);
            lines << QString("    {%1};").arg(fc.expression);
            if (withLegend && opts.legendMode != "none") lines << QString("\\addlegendentry{%1}").arg(esc(fc.legendLabel));
            lines << "";
        }
    };

    // With two axes the primary axis owns the only legend. Every plot is drawn with
    // "forget plot" and its legend entry is written afterwards with \\addlegendimage, so
    // the legend follows the user's curve order even when the curves alternate between axes.
    auto emitLegendInOrder = [&]() {
        if (opts.legendMode == "none") return;
        for (const auto &c : opts.curves)
            lines << QString("\\addlegendimage{%1}").arg(((isBar || isHist) ? (QStringList{"area legend"} + fillBits(c)) : curveStyleBits(c)).join(", "))
                  << QString("\\addlegendentry{%1}").arg(esc(c.legendLabel));
        for (const auto &fc : opts.fitCurves)
            lines << QString("\\addlegendimage{%1}").arg(fitStyleBits(fc).join(", "))
                  << QString("\\addlegendentry{%1}").arg(esc(fc.legendLabel));
    };

    // ---- primary-axis content ----
    if (dualAxis) {
        emitIntegrals(false);
        emitCurves(false, false, true);
        emitFits(false, false, true);
    } else {
        // No second axis in play: draw everything on the one axis, legend in plot order.
        emitIntegrals(false); emitIntegrals(true);
        emitCurves(false, true); emitCurves(true, true);
        emitFits(false, true); emitFits(true, true);
    }
    if (std::any_of(opts.fitCurves.begin(), opts.fitCurves.end(), [](const LatexFitCurveOpts &f) { return f.wasCustomFit; })) {
        lines << "% NOTE: custom fit expression(s) were auto-translated from the "
                 "app's expression syntax -- please double check them."
              << "";
    }

    lines << "\\end{axis}";

    // ================= second (right-hand) axis =================
    if (dualAxis) {
        lines << ""
              << (flip ? "% -- second value axis (top, overlaid on the same category range) --"
                       : "% -- second Y axis (overlaid on the same x range) --")
              << "\\begin{axis}["
              << sizeLine();
        if (flip) {
            lines << "    axis y line=none, axis x line*=top,";
            if (!opts.title.isEmpty()) {
                // pgfplots doesn't know about the top axis' tick labels / label when it places the
                // title, so lift the title by the room they take up.
                double lift = (opts.hideTicks ? 0.0 : 0.45) + (opts.y2label.isEmpty() ? 0.0 : 0.65);
                lines << QString("    title={%1},").arg(esc(opts.title));
                if (lift > 0) lines << QString("    title style={yshift=%1cm},").arg(fmt2(lift));
            }
            if (isBar) lines << QString("    xbar, bar width=%1pt,").arg(fmt2(opts.barWidthPt));
            lines << "    area legend,";
            emitLegendOptions();
            if (opts.hasYlim) lines << QString("    ymin=%1, ymax=%2,").arg(fmt6(opts.ymin), fmt6(opts.ymax));
            if (!opts.y2label.isEmpty()) lines << QString("    xlabel={%1},").arg(esc(opts.y2label));
            if (opts.hasY2lim) lines << QString("    xmin=%1, xmax=%2,").arg(fmt6(opts.y2min), fmt6(opts.y2max));
            if (opts.hideTicks) {
                lines << "    xtick=\\empty,";
            } else {
                if (opts.hasY2tickStep) lines << QString("    xtick distance=%1,").arg(fmt6(opts.y2tickStep));
                if (opts.y2MinorTicks) lines << QString("    minor x tick num=%1,").arg(opts.y2MinorTickNum);
                {
                    // one combined "tick label style": number format + rotation / anchor
                    QStringList parts;
                    if (!opts.y2DefaultDecimals) {
                        QString zf = opts.zeroFill ? ",fixed zerofill" : "";
                        parts << QString("/pgf/number format/.cd,fixed%1,precision=%2,/tikz/.cd").arg(zf).arg(opts.y2decimals);
                    }
                    if (opts.hasY2TickLabelRotate) parts << QString("rotate=%1").arg(fmt6(opts.y2TickLabelRotate));
                    if (opts.y2TickLabelAnchor != "default") parts << QString("anchor=%1").arg(opts.y2TickLabelAnchor);
                    if (!parts.isEmpty()) lines << QString("    x tick label style={%1},").arg(parts.join(","));
                }
                lines << QString("    tick align=%1,").arg(opts.tickAlign);
            }
        } else {
            lines << "    axis x line=none, axis y line*=right,";
            if (isBar) lines << QString("    ybar, bar width=%1pt,").arg(fmt2(opts.barWidthPt));
            if (isBar || isHist) lines << "    area legend,";
            emitLegendOptions();
            if (opts.hasXlim) lines << QString("    xmin=%1, xmax=%2,").arg(fmt6(opts.xmin), fmt6(opts.xmax));
            if (opts.logx) lines << "    xmode=log,";
            if (!opts.y2label.isEmpty()) lines << QString("    ylabel={%1},").arg(esc(opts.y2label));
            if (opts.hasY2labelRotate) lines << QString("    ylabel style={rotate=%1},").arg(fmt6(opts.y2labelRotate));
            if (opts.hasY2lim) lines << QString("    ymin=%1, ymax=%2,").arg(fmt6(opts.y2min), fmt6(opts.y2max));
            if (opts.logy2) lines << "    ymode=log,";
            if (opts.hideTicks) {
                lines << "    ytick=\\empty,";
            } else {
                if (opts.hasY2tickStep) lines << QString("    ytick distance=%1,").arg(fmt6(opts.y2tickStep));
                if (opts.y2MinorTicks) lines << QString("    minor y tick num=%1,").arg(opts.y2MinorTickNum);
                {
                    // one combined "tick label style": number format + rotation / anchor
                    QStringList parts;
                    if (!opts.y2DefaultDecimals) {
                        QString zf = opts.zeroFill ? ",fixed zerofill" : "";
                        parts << QString("/pgf/number format/.cd,fixed%1,precision=%2,/tikz/.cd").arg(zf).arg(opts.y2decimals);
                    }
                    if (opts.hasY2TickLabelRotate) parts << QString("rotate=%1").arg(fmt6(opts.y2TickLabelRotate));
                    if (opts.y2TickLabelAnchor != "default") parts << QString("anchor=%1").arg(opts.y2TickLabelAnchor);
                    if (!parts.isEmpty()) lines << QString("    y tick label style={%1},").arg(parts.join(","));
                }
                lines << QString("    tick align=%1,").arg(opts.tickAlign);
            }
        }
        for (const QString &l : barLabelLines()) lines << l;
        for (const QString &l : axisColorLines(true)) lines << l;
        lines << "]"
              << "";
        emitIntegrals(true);
        emitCurves(true, false, true);
        emitFits(true, false, true);
        emitLegendInOrder();
        lines << "\\end{axis}";
    }
    lines << "\\end{tikzpicture}";
    if (wrapFigure) {
        if (!opts.caption.isEmpty()) lines << QString("    \\caption{%1}").arg(esc(opts.caption));
        if (!opts.figLabel.isEmpty()) lines << QString("    \\label{%1}").arg(opts.figLabel);
        lines << "\\end{figure}";
    }
    lines << "\\end{document}";

    return lines.join("\n");
}
