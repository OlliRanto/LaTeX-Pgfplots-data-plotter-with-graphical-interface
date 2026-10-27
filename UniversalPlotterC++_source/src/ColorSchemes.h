#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QColor>
#include "LatexColors.h"

// A named list of curve colors. Tokens are what the LaTeX export and the color pickers
// understand: one of the 19 base xcolor names ("blue") or a 6-digit hex code ("1F77B4").
// qtHex (optional) lets the on-screen plot use slightly different colors than the export
// (only the default scheme does that: tab10 on screen, the matching xcolor names in LaTeX).
struct ColorScheme {
    QString name;
    QStringList tokens;
    QStringList qtHex;
};

inline QColor colorFromToken(const QString &token) {
    if (latexColors().contains(token)) return QColor("#" + latexColors()[token]);
    QString h = token;
    h.remove('#');
    return QColor("#" + h);
}

inline const QVector<ColorScheme> &colorSchemes() {
    static const QVector<ColorScheme> v = {
        // ---------------------------------------------------------------- LaTeX
        // pgfplots' own default plot cycle: blue, red, brown!60!black, black (then it repeats,
        // changing the line style). brown!60!black = 734D26.
        { "LaTeX default (pgfplots)",  { "blue", "red", "734D26", "black" }, {} },
        // pgfplots "cycle list name=color list"
        { "LaTeX color list",          { "red", "blue", "black", "yellow", "brown", "teal", "orange", "violet", "cyan", "007D00", "magenta", "gray" }, {} },
        // pgfplots "cycle list name=exotic": teal, orange, cyan!60!black, red!70!white, lime!80!black, ...
        { "LaTeX exotic",              { "teal", "orange", "009999", "FF4D4D", "99CC00", "red", "999900", "black", "blue" }, {} },
        { "LaTeX primary colors",      { "red", "green", "blue", "cyan", "magenta", "yellow", "black" }, {} },
        { "LaTeX dark",                { "blue", "red", "green", "purple", "olive", "teal", "brown", "darkgray" }, {} },

        // ------------------------------------------------------------ general purpose
        { "Default (tab10)",           { "blue", "orange", "green", "red", "purple", "brown", "pink", "gray", "olive", "cyan" },
          { "1F77B4", "FF7F0E", "2CA02C", "D62728", "9467BD", "8C564B", "E377C2", "7F7F7F", "BCBD22", "17BECF" } },
        { "Matplotlib tab10 (exact)",  { "1F77B4", "FF7F0E", "2CA02C", "D62728", "9467BD", "8C564B", "E377C2", "7F7F7F", "BCBD22", "17BECF" }, {} },
        { "Tableau 10",                { "4E79A7", "F28E2B", "E15759", "76B7B2", "59A14F", "EDC948", "B07AA1", "FF9DA7", "9C755F", "BAB0AC" }, {} },
        { "Seaborn deep",              { "4C72B0", "DD8452", "55A868", "C44E52", "8172B3", "937860", "DA8BC3", "8C8C8C", "CCB974", "64B5CD" }, {} },
        { "Seaborn muted",             { "4878D0", "EE854A", "6ACC64", "D65F5F", "956CB4", "8C613C", "DC7EC0", "797979", "D5BB67", "82C6E2" }, {} },
        { "Seaborn pastel",            { "A1C9F4", "FFB482", "8DE5A1", "FF9F9B", "D0BBFF", "DEBB9B", "FAB0E4", "CFCFCF", "FFFEA3", "B9F2F0" }, {} },
        { "Seaborn dark",              { "001C7F", "B1400D", "12711C", "8C0800", "591E71", "592F0D", "A23582", "3C3C3C", "B8850A", "006374" }, {} },
        { "ggplot2",                   { "F8766D", "B79F00", "00BA38", "00BFC4", "619CFF", "F564E3" }, {} },
        { "Material",                  { "2196F3", "F44336", "4CAF50", "FFC107", "9C27B0", "00BCD4", "FF5722", "795548", "607D8B", "8BC34A" }, {} },
        { "Candy",                     { "4D96FF", "FF6B6B", "6BCB77", "FFD93D", "9B5DE5", "F15BB5", "00BBF9" }, {} },
        { "Rainbow",                   { "FF0000", "FF7F00", "FFD700", "00A000", "0000FF", "4B0082", "8F00FF" }, {} },

        // ------------------------------------------------------------ ColorBrewer
        { "ColorBrewer Set1",          { "E41A1C", "377EB8", "4DAF4A", "984EA3", "FF7F00", "FFFF33", "A65628", "F781BF", "999999" }, {} },
        { "ColorBrewer Set2",          { "66C2A5", "FC8D62", "8DA0CB", "E78AC3", "A6D854", "FFD92F", "E5C494", "B3B3B3" }, {} },
        { "ColorBrewer Set3",          { "8DD3C7", "FFFFB3", "BEBADA", "FB8072", "80B1D3", "FDB462", "B3DE69", "FCCDE5", "D9D9D9", "BC80BD", "CCEBC5", "FFED6F" }, {} },
        { "ColorBrewer Dark2",         { "1B9E77", "D95F02", "7570B3", "E7298A", "66A61E", "E6AB02", "A6761D", "666666" }, {} },
        { "ColorBrewer Paired",        { "A6CEE3", "1F78B4", "B2DF8A", "33A02C", "FB9A99", "E31A1C", "FDBF6F", "FF7F00", "CAB2D6", "6A3D9A", "FFFF99", "B15928" }, {} },
        { "ColorBrewer Accent",        { "7FC97F", "BEAED4", "FDC086", "FFFF99", "386CB0", "F0027F", "BF5B17", "666666" }, {} },
        { "ColorBrewer Pastel1",       { "FBB4AE", "B3CDE3", "CCEBC5", "DECBE4", "FED9A6", "FFFFCC", "E5D8BD", "FDDAEC", "F2F2F2" }, {} },
        { "ColorBrewer Pastel2",       { "B3E2CD", "FDCDAC", "CBD5E8", "F4CAE4", "E6F5C9", "FFF2AE", "F1E2CC", "CCCCCC" }, {} },
        { "ColorBrewer Spectral",      { "9E0142", "D53E4F", "F46D43", "FDAE61", "FEE08B", "E6F598", "ABDDA4", "66C2A5", "3288BD", "5E4FA2" }, {} },

        // ------------------------------------------------------------ color-blind friendly
        { "Okabe-Ito (color-blind safe)", { "000000", "E69F00", "56B4E9", "009E73", "F0E442", "0072B2", "D55E00", "CC79A7" }, {} },
        { "Paul Tol bright",           { "4477AA", "EE6677", "228833", "CCBB44", "66CCEE", "AA3377", "BBBBBB" }, {} },
        { "Paul Tol vibrant",          { "EE7733", "0077BB", "33BBEE", "EE3377", "CC3311", "009988", "BBBBBB" }, {} },
        { "Paul Tol muted",            { "CC6677", "332288", "DDCC77", "117733", "88CCEE", "882255", "44AA99", "999933", "AA4499" }, {} },
        { "IBM color-blind safe",      { "648FFF", "785EF0", "DC267F", "FE6100", "FFB000" }, {} },

        // ------------------------------------------------------------ perceptual maps (discrete)
        { "Viridis",                   { "440154", "482878", "3E4A89", "31688E", "26828E", "1F9E89", "35B779", "6DCD59", "B4DE2C", "FDE725" }, {} },
        { "Plasma",                    { "0D0887", "5402A3", "8B0AA5", "B93289", "DB5C68", "F48849", "FEBC2A", "F0F921" }, {} },
        { "Inferno",                   { "000004", "280B54", "65156E", "9F2A63", "D44842", "F57D15", "FAC127", "FCFFA4" }, {} },
        { "Magma",                     { "000004", "221150", "5F187F", "982D80", "D3436E", "F8765C", "FEBA80", "FCFDBF" }, {} },
        { "Cividis",                   { "00204D", "31446B", "666970", "958F78", "CBBA69", "FFEA46" }, {} },

        // ------------------------------------------------------------ sequential
        { "Blues",                     { "08306B", "08519C", "2171B5", "4292C6", "6BAED6", "9ECAE1" }, {} },
        { "Greens",                    { "00441B", "006D2C", "238B45", "41AB5D", "74C476", "A1D99B" }, {} },
        { "Reds",                      { "67000D", "A50F15", "CB181D", "EF3B2C", "FB6A4A", "FCA082" }, {} },
        { "Oranges",                   { "7F2704", "A63603", "D94801", "F16913", "FD8D3C", "FDAE6B" }, {} },
        { "Purples",                   { "3F007D", "54278F", "6A51A3", "807DBA", "9E9AC8", "BCBDDC" }, {} },
        { "Warm",                      { "7F0000", "B30000", "D7301F", "EF6548", "FC8D59", "FDBB84" }, {} },
        { "Cool",                      { "084081", "0868AC", "2B8CBE", "4EB3D3", "7BCCC4", "A8DDB5" }, {} },
        { "Grayscale",                 { "000000", "444444", "777777", "A0A0A0", "C8C8C8" }, {} },

        // ------------------------------------------------------------ themes / journals
        { "Solarized",                 { "268BD2", "DC322F", "859900", "B58900", "6C71C4", "2AA198", "CB4B16", "D33682" }, {} },
        { "Nord",                      { "5E81AC", "BF616A", "A3BE8C", "EBCB8B", "B48EAD", "D08770", "88C0D0", "81A1C1" }, {} },
        { "Dracula",                   { "8BE9FD", "50FA7B", "FFB86C", "FF79C6", "BD93F9", "FF5555", "F1FA8C", "6272A4" }, {} },
        { "Monokai",                   { "66D9EF", "F92672", "A6E22E", "FD971F", "AE81FF", "E6DB74", "75715E" }, {} },
        { "Nature (NPG)",              { "E64B35", "4DBBD5", "00A087", "3C5488", "F39B7F", "8491B4", "91D1C2", "DC0000", "7E6148", "B09C85" }, {} },
        { "Lancet",                    { "00468B", "ED0000", "42B540", "0099B4", "925E9F", "FDAF91", "AD002A", "ADB6B6", "1B1919" }, {} },
        { "JAMA",                      { "374E55", "DF8F44", "00A1D5", "B24745", "79AF97", "6A6599", "80796B" }, {} },

        // ------------------------------------------------------------ moods
        { "Ocean",                     { "023E8A", "0077B6", "00B4D8", "48CAE4", "90E0EF", "CAF0F8" }, {} },
        { "Sunset",                    { "03071E", "6A040F", "D00000", "E85D04", "F48C06", "FFBA08" }, {} },
        { "Retro",                     { "264653", "2A9D8F", "E9C46A", "F4A261", "E76F51" }, {} },
        { "Autumn",                    { "780000", "C1121F", "003049", "669BBC", "B08968" }, {} },
        { "Earth tones",               { "8C510A", "BF812D", "DFC27D", "80CDC1", "35978F", "01665E" }, {} },
        { "Neon",                      { "08F7FE", "FE53BB", "F5D300", "09FBD3", "7122FA", "FF2281" }, {} },
    };
    return v;
}

// Index of the scheme with this name, or -1 ("Custom" and unknown names).
inline int colorSchemeIndex(const QString &name) {
    const auto &v = colorSchemes();
    for (int i = 0; i < v.size(); ++i) if (v[i].name == name) return i;
    return -1;
}

// The name the plot window starts with: what it has always used.
inline QString defaultColorSchemeName() { return QStringLiteral("Default (tab10)"); }

// The color the n-th curve gets on screen / in the export.
inline QColor schemeQtColor(const ColorScheme &s, int n) {
    if (!s.qtHex.isEmpty()) return QColor("#" + s.qtHex[n % s.qtHex.size()]);
    return colorFromToken(s.tokens[n % s.tokens.size()]);
}
inline QString schemeToken(const ColorScheme &s, int n) { return s.tokens[n % s.tokens.size()]; }
