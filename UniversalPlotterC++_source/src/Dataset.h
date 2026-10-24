#pragma once
#include <QString>
#include <QVector>
#include <QStringList>
#include <QColor>
#include <QMap>
#include <limits>
#include <cmath>

// Sentinel used as the "column name" for a file's row-index pseudo-column,
// mirroring the Python app's INDEX_LABEL.
inline QString indexLabel() { return QStringLiteral("(row index)"); }

// Marker stored in Dataset::path for data typed/pasted into the "Enter data manually"
// window (it has no file on disk).
inline QString manualDataPath() { return QStringLiteral("(manual entry)"); }

// One loaded column: numeric values (NaN where missing/non-numeric).
struct ColumnData {
    QString name;
    QVector<double> values; // may contain NaN
    bool isNumeric = true;
    QStringList labels;     // raw cell text; kept only for non-numeric columns (category labels)
};

// Info about how a file was parsed, mirroring load_dataframe()'s info dict.
struct LoadInfo {
    QString sep;          // literal separator, or "\\s+" for whitespace
    QString decimal;      // "." or ","
    bool hasHeader = true;
    QString encoding;
    int rows = 0;
    int cols = 0;
    int skippedRows = 0;
};

// One loaded file/dataset.
struct Dataset {
    QString path;
    QString basename;
    QVector<ColumnData> columns;
    LoadInfo info;
    QColor color;
    QString xColumn; // name of chosen X column, or indexLabel()

    int columnIndex(const QString &name) const {
        for (int i = 0; i < columns.size(); ++i)
            if (columns[i].name == name) return i;
        return -1;
    }
};

// A (datasetIndex, columnName) key identifying one plotted Y curve.
struct CurveKey {
    int dsIdx = -1;
    QString col;
    bool y2 = false;   // true: this curve is plotted against the second (right) Y axis
    bool operator==(const CurveKey &o) const { return dsIdx == o.dsIdx && col == o.col && y2 == o.y2; }
    bool operator<(const CurveKey &o) const {
        if (dsIdx != o.dsIdx) return dsIdx < o.dsIdx;
        if (y2 != o.y2) return y2 < o.y2;
        return col < o.col;
    }
};
inline uint qHash(const CurveKey &k, uint seed = 0) {
    return qHash(k.dsIdx, seed) ^ qHash(k.col, seed) ^ qHash(k.y2, seed + 1);
}

inline const QVector<QColor> &fileColors() {
    static const QVector<QColor> colors = {
        QColor("#1f77b4"), QColor("#ff7f0e"), QColor("#2ca02c"), QColor("#d62728"),
        QColor("#9467bd"), QColor("#8c564b"), QColor("#e377c2"), QColor("#7f7f7f"),
        QColor("#bcbd22"), QColor("#17becf"),
    };
    return colors;
}
