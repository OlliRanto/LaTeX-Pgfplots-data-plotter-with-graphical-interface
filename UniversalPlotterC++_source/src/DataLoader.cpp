#include "DataLoader.h"
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QRegularExpression>
#include <QFileInfo>
#include <QMap>
#include <stdexcept>
#include <algorithm>

namespace {

struct SampleEntry { int lineNo; QString text; };

// Mirrors read_sample_lines(): first n non-blank lines, trying encodings.
QVector<SampleEntry> readSampleLines(const QString &path, QString &encodingOut, int n = 300) {
    const QStringList encodings = {"UTF-8", "UTF-8", "ISO-8859-1"}; // utf-8-sig handled by BOM auto-skip
    QVector<SampleEntry> entries;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        throw std::runtime_error("Could not open file.");
    QByteArray raw = f.readAll();
    // Strip UTF-8 BOM if present (utf-8-sig behavior)
    if (raw.startsWith("\xEF\xBB\xBF")) raw = raw.mid(3);

    QString text = QString::fromUtf8(raw);
    // crude "did utf-8 decoding look wrong" check: fromUtf8 never fails, it
    // just substitutes replacement chars for invalid sequences. Count those.
    int replCount = text.count(QChar(0xFFFD));
    if (replCount > 0) {
        text = QString::fromLatin1(raw);
        encodingOut = "latin-1";
    } else {
        encodingOut = "utf-8";
    }

    QStringList rawLines = text.split(QRegularExpression("\r\n|\n|\r"));
    int lineNo = 0;
    for (const QString &raw2 : rawLines) {
        if (!raw2.trimmed().isEmpty()) entries.push_back({lineNo, raw2});
        lineNo++;
        if (entries.size() >= n) break;
    }
    return entries;
}

} // namespace

QStringList Detect::tokenizeLine(const QString &text, const QString &sep) {
    if (sep == "\\s+") {
        QStringList parts = text.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        return parts;
    }
    return text.split(sep);
}

bool Detect::isNumericToken(const QString &tokIn) {
    QString tok = tokIn.trimmed();
    if (tok.isEmpty()) return false;
    static const QRegularExpression re("^-?\\d+([.,]\\d+)?([eE][+-]?\\d+)?$");
    return re.match(tok).hasMatch();
}

QString Detect::detectSeparator(const QStringList &lines) {
    const QStringList candidates = {",", ";", "\t", "|"};
    int sampleN = std::min(40, int(lines.size()));

    QString bestSep;
    int bestScore = 0;
    for (const QString &d : candidates) {
        QMap<int, int> countFreq; // count-of-delimiter -> how many lines have it
        for (int i = 0; i < sampleN; ++i) {
            const QString &line = lines[i];
            if (line.trimmed().isEmpty()) continue;
            int c = line.count(d);
            if (c > 0) countFreq[c]++;
        }
        if (countFreq.isEmpty()) continue;
        int commonCount = 0, commonFreq = 0;
        for (auto it = countFreq.constBegin(); it != countFreq.constEnd(); ++it) {
            if (it.value() > commonFreq) { commonFreq = it.value(); commonCount = it.key(); }
        }
        if (commonFreq > bestScore && commonCount > 0) {
            bestScore = commonFreq;
            bestSep = d;
        }
    }

    if (!bestSep.isEmpty() && bestScore >= std::max(2, int(sampleN * 0.3)))
        return bestSep;

    return "\\s+";
}

QString Detect::detectDecimal(const QStringList &lines, const QString &sep) {
    if (sep == ",") return ".";

    QString text;
    int n = std::min(80, int(lines.size()));
    for (int i = 0; i < n; ++i) { text += lines[i]; text += '\n'; }

    static const QRegularExpression dotRe("\\d+\\.\\d+");
    static const QRegularExpression commaRe("\\d+,\\d+(?!\\d*,)");

    int dotHits = 0, commaHits = 0;
    auto it = dotRe.globalMatch(text);
    while (it.hasNext()) { it.next(); dotHits++; }
    auto it2 = commaRe.globalMatch(text);
    while (it2.hasNext()) { it2.next(); commaHits++; }

    return (commaHits > dotHits) ? "," : ".";
}

namespace {

// Mirrors analyze_structure(): returns (hasHeader, skiprows-as-set-of-line-numbers).
void analyzeStructure(const QVector<SampleEntry> &entries, const QString &sep,
                       bool &hasHeader, QSet<int> &skipRows) {
    hasHeader = false;
    skipRows.clear();
    if (entries.isEmpty()) return;

    QVector<SampleEntry> nonComment;
    for (const auto &e : entries)
        if (!e.text.trimmed().startsWith('#')) nonComment.push_back(e);
    if (nonComment.isEmpty()) nonComment = entries;

    struct Shaped { int lineNo; QString text; int count; };
    QVector<Shaped> shaped;
    QMap<int, int> countFreq;
    for (const auto &e : nonComment) {
        int c = Detect::tokenizeLine(e.text, sep).size();
        shaped.push_back({e.lineNo, e.text, c});
        if (c >= 1) countFreq[c]++;
    }
    if (countFreq.isEmpty()) return;

    int modalCount = 0, modalFreq = 0;
    for (auto it = countFreq.constBegin(); it != countFreq.constEnd(); ++it) {
        if (it.value() > modalFreq) { modalFreq = it.value(); modalCount = it.key(); }
    }

    QVector<Shaped> block;
    for (const auto &s : shaped) if (s.count == modalCount) block.push_back(s);
    if (block.isEmpty()) return;

    int headerLineNo = block.first().lineNo;
    QString headerText = block.first().text;
    QStringList tokens = Detect::tokenizeLine(headerText, sep);
    int numericCount = 0;
    for (const QString &t : tokens) if (Detect::isNumericToken(t)) numericCount++;
    double numericRatio = tokens.isEmpty() ? 0.0 : double(numericCount) / tokens.size();
    hasHeader = numericRatio < 0.5;

    for (int i = 0; i < headerLineNo; ++i) skipRows.insert(i);
}

double parseNumber(QString tok, const QString &decimal) {
    tok = tok.trimmed();
    if (tok.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
    if (decimal == ",") {
        // "," is decimal mark: remove thousands dots, then swap comma->dot
        tok.replace(".", "");
        tok.replace(",", ".");
    }
    bool ok = false;
    double v = tok.toDouble(&ok);
    return ok ? v : std::numeric_limits<double>::quiet_NaN();
}

} // namespace

Dataset loadDataset(const QString &path, const QString &sepOverride, const QString &decimalOverride) {
    QString encoding;
    QVector<SampleEntry> sampleEntries = readSampleLines(path, encoding);
    if (sampleEntries.isEmpty())
        throw std::runtime_error("File appears to be empty.");

    QStringList sampleTexts;
    for (const auto &e : sampleEntries) sampleTexts << e.text;

    QString sep = !sepOverride.isEmpty() ? sepOverride : Detect::detectSeparator(sampleTexts);
    QString decimal = !decimalOverride.isEmpty() ? decimalOverride : Detect::detectDecimal(sampleTexts, sep);

    bool hasHeader = false;
    QSet<int> skipRows;
    analyzeStructure(sampleEntries, sep, hasHeader, skipRows);

    // Now read the *whole* file (not just the sample).
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        throw std::runtime_error("Could not open file.");
    QByteArray raw = f.readAll();
    if (raw.startsWith("\xEF\xBB\xBF")) raw = raw.mid(3);
    QString fullText = (encoding == "latin-1") ? QString::fromLatin1(raw) : QString::fromUtf8(raw);
    QStringList allLines = fullText.split(QRegularExpression("\r\n|\n|\r"));

    QStringList headerNames;
    QVector<QStringList> dataRows;
    int expectedCols = -1;
    bool headerCaptured = !hasHeader;

    for (int lineNo = 0; lineNo < allLines.size(); ++lineNo) {
        if (skipRows.contains(lineNo)) continue;
        QString line = allLines[lineNo];
        if (line.trimmed().isEmpty()) continue;
        if (line.trimmed().startsWith('#')) continue;

        QStringList tokens = Detect::tokenizeLine(line, sep);
        if (tokens.isEmpty()) continue;

        if (!headerCaptured) {
            headerNames = tokens;
            expectedCols = tokens.size();
            headerCaptured = true;
            continue;
        }
        if (expectedCols < 0) expectedCols = tokens.size();
        if (tokens.size() != expectedCols) continue; // on_bad_lines="skip"
        dataRows.push_back(tokens);
    }

    if (expectedCols <= 0)
        throw std::runtime_error("Could not find any columns in this file.");

    QStringList colNames;
    for (int i = 0; i < expectedCols; ++i) {
        if (hasHeader && i < headerNames.size() && !headerNames[i].trimmed().isEmpty())
            colNames << headerNames[i].trimmed();
        else
            colNames << QString("Col%1").arg(i + 1);
    }

    Dataset ds;
    ds.path = path;
    ds.basename = QFileInfo(path).fileName();
    ds.columns.resize(expectedCols);
    for (int c = 0; c < expectedCols; ++c) {
        ds.columns[c].name = colNames[c];
        ds.columns[c].values.reserve(dataRows.size());
    }

    for (const QStringList &row : dataRows) {
        for (int c = 0; c < expectedCols; ++c) {
            double v = parseNumber(row[c], decimal);
            ds.columns[c].values.push_back(v);
        }
    }

    // Decide numeric-ness per column: >=80% parseable -> numeric.
    for (auto &col : ds.columns) {
        if (col.values.isEmpty()) { col.isNumeric = true; continue; }
        int nonNan = 0;
        for (double v : col.values) if (!std::isnan(v)) nonNan++;
        col.isNumeric = (double(nonNan) / col.values.size()) >= 0.8;
    }

    // Keep the raw text of non-numeric columns so they can serve as bar-chart category labels.
    for (int c = 0; c < ds.columns.size(); ++c) {
        if (ds.columns[c].isNumeric) continue;
        ds.columns[c].labels.reserve(dataRows.size());
        for (const QStringList &row : dataRows) ds.columns[c].labels << row[c].trimmed();
    }

    if (ds.columns.isEmpty() || dataRows.isEmpty())
        throw std::runtime_error("No usable rows/columns found.");

    ds.info.sep = sep;
    ds.info.decimal = decimal;
    ds.info.hasHeader = hasHeader;
    ds.info.encoding = encoding;
    ds.info.rows = dataRows.size();
    ds.info.cols = expectedCols;
    ds.info.skippedRows = skipRows.size();
    ds.xColumn = ds.columns.first().name;

    return ds;
}
