#pragma once
#include <QString>
#include <QMap>
#include <QColor>
#include <QRegularExpression>

// The 19 colors xcolor/pgfplots provide out of the box with plain
// \usepackage{pgfplots} -- no \definecolor, no hex codes, no extra xcolor
// package options. Mirrors LatexExportDialog.LATEX_COLORS in the Python app.
inline const QMap<QString, QString> &latexColors() {
    static const QMap<QString, QString> m = {
        {"red", "FF0000"}, {"green", "00B200"}, {"blue", "0000FF"},
        {"cyan", "00FFFF"}, {"magenta", "FF00FF"}, {"yellow", "FFFF00"},
        {"black", "000000"}, {"gray", "808080"}, {"darkgray", "404040"},
        {"lightgray", "BFBFBF"}, {"brown", "BF8040"}, {"lime", "BFFF00"},
        {"olive", "808000"}, {"orange", "FF8000"}, {"pink", "FFBFBF"},
        {"purple", "400080"}, {"teal", "008080"}, {"violet", "800080"},
        {"white", "FFFFFF"},
    };
    return m;
}

// Our plotted-curve color cycle (see Dataset.h::fileColors(), which is
// matplotlib's default tab10 cycle) mapped index-for-index to the LaTeX
// color name matplotlib would show for "tab:blue" etc -- so a curve's
// on-screen color always pre-selects the equivalent named LaTeX color,
// exactly like the Python app's _match_latex_color() does by name.
inline const QVector<QString> &tab10LatexNames() {
    static const QVector<QString> v = {
        "blue", "orange", "green", "red", "purple",
        "brown", "pink", "gray", "olive", "cyan",
    };
    return v;
}

inline bool isHexColor(const QString &s) {
    static const QRegularExpression re("^[0-9A-Fa-f]{6}$");
    return re.match(s.trimmed()).hasMatch();
}

// A valid, collision-resistant xcolor name for a 6-digit hex code.
inline QString pgfColorName(const QString &hex6) {
    return "c" + hex6.toUpper();
}

// A curve's color field is either a base LaTeX color name or a custom
// 6-digit hex code; names are used as-is, hex codes are mapped to the
// \definecolor'd name build_pgfplots_code() emits for them.
inline QString resolveColorToken(const QString &colorStr) {
    QString c = colorStr.trimmed();
    return isHexColor(c) ? pgfColorName(c.toUpper()) : c;
}

// Escape LaTeX special characters in plain text (labels, titles, legend entries).
// Only applied when the "Escape special characters" option is ticked in the export
// dialog; otherwise text is written exactly as typed so math like $\\alpha_1$ works.
inline QString texEscape(const QString &sIn) {
    QString out;
    out.reserve(sIn.size());
    for (QChar ch : sIn) {
        switch (ch.unicode()) {
        case '\\': out += "\\textbackslash{}"; break;
        case '&': out += "\\&"; break;
        case '%': out += "\\%"; break;
        case '$': out += "\\$"; break;
        case '#': out += "\\#"; break;
        case '_': out += "\\_"; break;
        case '{': out += "\\{"; break;
        case '}': out += "\\}"; break;
        case '~': out += "\\textasciitilde{}"; break;
        case '^': out += "\\textasciicircum{}"; break;
        default: out += ch;
        }
    }
    return out;
}

inline const QStringList &latexMarks() {
    static const QStringList v = {
        "none", "*", "x", "+", "-", "|", "o",
        "asterisk", "star",
        "oplus", "oplus*", "otimes", "otimes*",
        "square", "square*",
        "triangle", "triangle*",
        "diamond", "diamond*",
        "pentagon", "pentagon*",
        "halfcircle", "halfcircle*",
        "Mercedes star", "Mercedes star flipped",
        "ball",
    };
    return v;
}

inline const QStringList &latexLineStyles() {
    static const QStringList v = { "solid", "dashed", "dotted", "dashdotted", "none" };
    return v;
}

// Fill of bars / histogram bins: a plain color, nothing, or one of the TikZ "patterns" library fills.
inline const QStringList &latexFillTypes() {
    static const QStringList v = { "solid", "none", "north east lines", "north west lines", "horizontal lines",
                                   "vertical lines", "grid", "crosshatch", "dots", "crosshatch dots", "bricks",
                                   "fivepointed stars", "sixpointed stars" };
    return v;
}
inline bool latexFillIsPattern(const QString &t) { return t != "solid" && t != "none" && !t.isEmpty(); }
