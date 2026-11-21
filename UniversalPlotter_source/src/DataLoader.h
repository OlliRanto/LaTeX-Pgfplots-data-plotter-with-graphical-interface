#pragma once
#include "Dataset.h"
#include <QString>
#include <QStringList>

// Separator/decimal override choices, mirroring SEP_CHOICES / DECIMAL_CHOICES.
// Empty QString means "Auto" (auto-detect).
namespace Detect {

// Guess the field separator from a sample of lines. Returns one of
// "," ";" "\t" "|" or "\\s+" (whitespace-run).
QString detectSeparator(const QStringList &lines);

// Guess whether numbers use '.' or ',' as the decimal mark.
QString detectDecimal(const QStringList &lines, const QString &sep);

// Split one line into fields for a given separator (handles "\\s+").
QStringList tokenizeLine(const QString &text, const QString &sep);

bool isNumericToken(const QString &tok);

} // namespace Detect

// Loads a CSV/TXT/TSV/DAT file into a Dataset, auto-detecting separator,
// decimal mark, header presence, and any preamble lines to skip (unless
// overridden). Throws std::runtime_error on failure (mirrors load_dataframe
// raising ValueError).
Dataset loadDataset(const QString &path, const QString &sepOverride = QString(),
                     const QString &decimalOverride = QString());
