#include "DataEntryDialog.h"
#include "MainWindow.h"
#include "DataLoader.h"

#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QFont>
#include <QShowEvent>
#include <QRegularExpression>
#include <QTime>
#include <functional>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <algorithm>

namespace {

constexpr int kInitialRows = 30;
constexpr int kInitialCols = 5;
constexpr int kMaxRows = 200000;
constexpr int kMaxCols = 500;

// "A", "B", ... "Z", "AA", ... like a spreadsheet.
QString columnLetters(int idx) {
    QString s;
    for (int n = idx; n >= 0; n = n / 26 - 1) s.prepend(QChar('A' + n % 26));
    return s;
}

// Splits clipboard text into rows of cells. Handles Excel's tab-separated format,
// including quoted cells that contain tabs / line breaks / doubled quotes.
// Falls back to ';' if there are no tabs; otherwise each line is one cell.
QVector<QStringList> parseDelimited(const QString &textIn, QChar delim) {
    QString text = textIn;
    text.replace("\r\n", "\n").replace('\r', '\n');

    QVector<QStringList> rows;
    QStringList row;
    QString cell;
    bool inQuotes = false, cellWasQuoted = false;
    auto endCell = [&]() { row << cell; cell.clear(); cellWasQuoted = false; };
    auto endRow = [&]() { endCell(); rows << row; row.clear(); };

    for (int i = 0; i < text.size(); ++i) {
        QChar ch = text[i];
        if (inQuotes) {
            if (ch == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') { cell += '"'; ++i; }
                else inQuotes = false;
            } else cell += ch;
        } else if (ch == '"' && cell.isEmpty() && !cellWasQuoted) {
            inQuotes = true; cellWasQuoted = true;
        } else if (!delim.isNull() && ch == delim) {
            endCell();
        } else if (ch == '\n') {
            endRow();
        } else {
            cell += ch;
        }
    }
    // Excel ends the block with a newline; don't turn that into an extra empty row.
    if (!cell.isEmpty() || !row.isEmpty() || cellWasQuoted) endRow();
    return rows;
}

QVector<QStringList> parseClipboard(const QString &text) {
    QChar delim = QChar('\t');
    if (!text.contains('\t')) delim = text.contains(';') ? QChar(';') : QChar(0);
    return parseDelimited(text, delim);
}

// Separator choices of the dialog, in combo order.
const QStringList kSeps = {",", ";", "\t", "|", " "};

// A file's text split into cells with the separator found by the loader ("\\s+" = runs of white space).
QVector<QStringList> parseFileText(const QString &text, const QString &sep) {
    if (sep == "\\s+") {
        QVector<QStringList> rows;
        for (const QString &line : text.split(QRegularExpression("\r\n|\n|\r"))) {
            const QString t = line.trimmed();
            rows << (t.isEmpty() ? QStringList() : t.split(QRegularExpression("\\s+")));
        }
        while (!rows.isEmpty() && rows.last().isEmpty()) rows.removeLast();
        return rows;
    }
    return parseDelimited(text, sep.isEmpty() ? QChar(',') : sep[0]);
}

// Lenient number parser for a single cell: handles decimal commas ("1,5"),
// thousands separators ("1,234.5" / "1.234,5"), spaces and the unicode minus.
double parseCell(QString t) {
    t.replace(QChar(0x2212), '-');
    t.remove(QRegularExpression("\\s"));
    if (t.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
    int lc = t.lastIndexOf(','), ld = t.lastIndexOf('.');
    if (lc >= 0 && ld >= 0) {
        if (lc > ld) { t.remove('.'); t.replace(',', '.'); }   // 1.234,5
        else t.remove(',');                                    // 1,234.5
    } else if (lc >= 0) {
        if (t.count(',') == 1) t.replace(',', '.');            // 1,5
        else t.remove(',');                                    // 1,234,567
    }
    bool ok = false;
    double v = t.toDouble(&ok);
    return ok ? v : std::numeric_limits<double>::quiet_NaN();
}

// What the clean-up removed.
struct CleanStats {
    int removedRows = 0;      // preamble / comment / blank / trailing lines
    int removedCols = 0;      // columns without any data
    bool header = false;      // the first remaining row holds the column names
};

// Cleans the cells of a file: trims them, removes blank and "#" comment lines, the preamble above the data
// (title, instrument settings, ...), lines after the data, and columns without data. The row with the column names
// right above the data is kept (st.header). The data starts at the first row of numbers that is followed by rows
// of the same width.
void cleanRows(QVector<QStringList> &rows, CleanStats &st) {
    const int before = rows.size();
    QVector<QStringList> r;
    for (QStringList row : rows) {
        for (QString &c : row) c = c.trimmed();
        while (!row.isEmpty() && row.last().isEmpty()) row.removeLast();
        if (row.isEmpty() || row[0].startsWith('#')) continue;
        r << row;
    }
    auto counts = [&](int i, int &nonEmpty, int &numeric) {
        nonEmpty = numeric = 0;
        for (const QString &t : r[i]) {
            if (t.isEmpty()) continue;
            ++nonEmpty;
            if (!std::isnan(parseCell(t))) ++numeric;
        }
    };
    auto isData = [&](int i) { int ne, nu; counts(i, ne, nu); return nu >= 1 && 2 * nu >= ne; };
    const int n = r.size();

    int start = -1;
    for (int i = 0; i < n && start < 0; ++i) {
        if (!isData(i)) continue;
        bool ok = true;
        for (int j = 1; j <= 2 && i + j < n; ++j)
            if (!isData(i + j) || std::abs(int(r[i + j].size()) - int(r[i].size())) > 1) { ok = false; break; }
        if (ok) start = i;
    }
    for (int i = 0; i < n && start < 0; ++i) if (isData(i)) start = i;

    QVector<QStringList> kept;
    int hdr = -1;
    if (start < 0) {
        kept = r;                                           // no numbers at all: only trim
    } else {
        if (start > 0) {                                    // the column names: the row right above the data
            int ne, nu;
            counts(start - 1, ne, nu);
            int dne, dnu;
            counts(start, dne, dnu);
            if (ne >= 1 && 2 * nu < ne && 2 * ne >= dne) hdr = start - 1;
            if (hdr >= 1) {                                 // names above units ("Time" / "s"): one name "Time [s]"
                int pe, pn;
                counts(hdr - 1, pe, pn);
                if (pn == 0 && pe == ne && std::abs(int(r[hdr - 1].size()) - int(r[hdr].size())) <= 1) {
                    QStringList merged;
                    const int w = std::max(int(r[hdr - 1].size()), int(r[hdr].size()));
                    for (int c = 0; c < w; ++c) {
                        const QString a = c < r[hdr - 1].size() ? r[hdr - 1][c] : QString();
                        const QString b = c < r[hdr].size() ? r[hdr][c] : QString();
                        merged << (a.isEmpty() ? b : b.isEmpty() ? a : a + " [" + b + "]");
                    }
                    r[hdr] = merged;
                }
            }
        }
        int last = start;
        for (int i = start; i < n; ++i) if (isData(i)) last = i;
        for (int i = (hdr >= 0 ? hdr : start); i <= last; ++i) kept << r[i];
    }
    st.header = hdr >= 0;

    // columns without any data (the names row does not count)
    int ncols = 0;
    for (const auto &row : kept) ncols = std::max(ncols, int(row.size()));
    for (auto &row : kept) while (row.size() < ncols) row << QString();
    const int firstData = st.header ? 1 : 0;
    st.removedCols = 0;
    for (int c = ncols - 1; c >= 0; --c) {
        bool any = false;
        for (int i = firstData; i < kept.size() && !any; ++i) any = !kept[i][c].isEmpty();
        if (any) continue;
        for (auto &row : kept) row.removeAt(c);
        ++st.removedCols;
    }
    st.removedRows = before - kept.size();
    rows = kept;
}

// QTableWidget with spreadsheet behaviour: Ctrl+C / Ctrl+X / Ctrl+V (Excel
// compatible), Delete clears cells, Enter moves down like Excel.
class PasteTable : public QTableWidget {
public:
    PasteTable(int rows, int cols, QWidget *parent = nullptr) : QTableWidget(rows, cols, parent) {
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }

    QTableWidgetItem *ensureItem(int r, int c) {
        QTableWidgetItem *it = item(r, c);
        if (!it) { it = new QTableWidgetItem; setItem(r, c, it); }
        return it;
    }

    void pasteFromClipboard() {
        QString text = QGuiApplication::clipboard()->text();
        if (text.isEmpty()) return;
        QVector<QStringList> rows = parseClipboard(text);
        if (rows.isEmpty()) return;

        int startRow = std::max(0, currentRow()), startCol = std::max(0, currentColumn());
        const auto ranges = selectedRanges();
        if (!ranges.isEmpty()) {
            startRow = ranges.first().topRow();
            startCol = ranges.first().leftColumn();
            for (const auto &rg : ranges) {
                startRow = std::min(startRow, rg.topRow());
                startCol = std::min(startCol, rg.leftColumn());
            }
        }
        int maxCols = 0;
        for (const auto &r : rows) maxCols = std::max(maxCols, int(r.size()));

        int needRows = startRow + rows.size(), needCols = startCol + maxCols;
        if (needRows > kMaxRows || needCols > kMaxCols) {
            QMessageBox::warning(this, "Paste too large",
                                 QString("The pasted block would need %1 rows x %2 columns; the limit is %3 x %4.")
                                     .arg(needRows).arg(needCols).arg(kMaxRows).arg(kMaxCols));
            return;
        }
        setUpdatesEnabled(false);
        if (needRows > rowCount()) setRowCount(needRows);
        if (needCols > columnCount()) setColumnCount(needCols);
        for (int r = 0; r < rows.size(); ++r)
            for (int c = 0; c < rows[r].size(); ++c)
                ensureItem(startRow + r, startCol + c)->setText(rows[r][c]);
        setUpdatesEnabled(true);
        if (onStructureChanged) onStructureChanged();
        clearSelection();
        setRangeSelected(QTableWidgetSelectionRange(startRow, startCol, needRows - 1, needCols - 1), true);
    }

    QString selectionAsText() const {
        const auto idx = selectedIndexes();
        if (idx.isEmpty()) return QString();
        int r0 = rowCount(), r1 = -1, c0 = columnCount(), c1 = -1;
        QSet<QPair<int, int>> sel;
        for (const auto &i : idx) {
            r0 = std::min(r0, i.row()); r1 = std::max(r1, i.row());
            c0 = std::min(c0, i.column()); c1 = std::max(c1, i.column());
            sel.insert({i.row(), i.column()});
        }
        QString out;
        for (int r = r0; r <= r1; ++r) {
            QStringList cells;
            for (int c = c0; c <= c1; ++c) {
                QString t = sel.contains({r, c}) && item(r, c) ? item(r, c)->text() : QString();
                if (t.contains('\t') || t.contains('\n') || t.contains('"'))
                    t = '"' + t.replace("\"", "\"\"") + '"';
                cells << t;
            }
            out += cells.join('\t') + '\n';
        }
        return out;
    }

    void clearSelectedCells() {
        for (const auto &i : selectedIndexes())
            if (QTableWidgetItem *it = item(i.row(), i.column())) it->setText(QString());
    }

    std::function<void()> onStructureChanged;

protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (e->matches(QKeySequence::Paste)) { pasteFromClipboard(); return; }
        if (e->matches(QKeySequence::Copy)) { QGuiApplication::clipboard()->setText(selectionAsText()); return; }
        if (e->matches(QKeySequence::Cut)) {
            QGuiApplication::clipboard()->setText(selectionAsText());
            clearSelectedCells();
            return;
        }
        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
            if (state() != QAbstractItemView::EditingState) { clearSelectedCells(); return; }
        }
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && state() != QAbstractItemView::EditingState) {
            moveDown();
            return;
        }
        QTableWidget::keyPressEvent(e);
    }

    // Enter while editing commits and moves down (like Excel).
    void closeEditor(QWidget *editor, QAbstractItemDelegate::EndEditHint hint) override {
        bool enter = (hint == QAbstractItemDelegate::SubmitModelCache);
        QTableWidget::closeEditor(editor, hint);
        if (enter) moveDown();
    }

private:
    void moveDown() {
        int r = currentRow(), c = std::max(0, currentColumn());
        if (r < 0) r = -1;
        if (r + 1 >= rowCount() && rowCount() < kMaxRows) {
            setRowCount(rowCount() + 1);
            if (onStructureChanged) onStructureChanged();
        }
        if (r + 1 < rowCount()) setCurrentCell(r + 1, c);
    }
};

} // namespace

// ============================================================ DataEntryDialog
DataEntryDialog::DataEntryDialog(MainWindow *app) : QDialog(app), m_app(app) {
    setWindowTitle("Enter / edit data");
    resize(820, 600);

    auto *layout = new QVBoxLayout(this);

    // ---- view / edit a CSV file ----
    auto *fileRow = new QHBoxLayout;
    auto *openBtn = new QPushButton("Open CSV...");
    openBtn->setToolTip("Show a CSV / TXT file in the table so that it can be viewed and edited");
    connect(openBtn, &QPushButton::clicked, this, &DataEntryDialog::openCsv);
    fileRow->addWidget(openBtn);
    fileRow->addWidget(new QLabel("or a loaded file:"));
    m_loadedCombo = new QComboBox;
    m_loadedCombo->setMinimumWidth(180);
    m_loadedCombo->setToolTip("The files that are loaded in the plotter");
    fileRow->addWidget(m_loadedCombo, 1);
    auto *editBtn = new QPushButton("Edit");
    editBtn->setToolTip("Show the selected loaded file in the table");
    connect(editBtn, &QPushButton::clicked, this, &DataEntryDialog::editLoadedFile);
    fileRow->addWidget(editBtn);
    layout->addLayout(fileRow);
    auto *cleanRow = new QHBoxLayout;
    m_cleanCheck = new QCheckBox("Clean up when opening");
    m_cleanCheck->setChecked(true);
    m_cleanCheck->setToolTip("Remove the lines above the data (title, settings), comment lines, lines after the data and\n"
                             "empty columns. The row with the column names is kept. Untick to see the file as it is.");
    cleanRow->addWidget(m_cleanCheck);
    auto *cleanBtn = new QPushButton("Clean up");
    cleanBtn->setToolTip("Clean up the table as it is now (same rules)");
    connect(cleanBtn, &QPushButton::clicked, this, &DataEntryDialog::cleanTable);
    cleanRow->addWidget(cleanBtn);
    cleanRow->addStretch();
    layout->addLayout(cleanRow);
    m_fileLabel = new QLabel("Typed data (no file)");
    m_fileLabel->setStyleSheet("color:#777;");
    m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_fileLabel);

    auto *hint = new QLabel(
        "Type values or paste straight from Excel (click a cell, then Ctrl+V). "
        "Ctrl+C / Ctrl+X / Delete work on the selected cells. Decimal commas are understood. "
        "Open a CSV file to view and change it (header lines and empty columns are cleaned away unless you untick the option); Save writes it back.");
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#777;");
    layout->addWidget(hint);

    m_headerCheck = new QCheckBox("First row contains column names");
    m_headerCheck->setChecked(true);
    layout->addWidget(m_headerCheck);

    auto *table = new PasteTable(kInitialRows, kInitialCols);
    m_table = table;
    table->onStructureChanged = [this]() { updateColumnHeaders(); updateHeaderRowStyle(); };
    for (int r = 0; r < kInitialRows; ++r)
        for (int c = 0; c < kInitialCols; ++c) table->ensureItem(r, c);
    table->horizontalHeader()->setDefaultSectionSize(110);
    layout->addWidget(table, 1);

    auto *editRow = new QHBoxLayout;
    auto addBtn = [&](const QString &text, auto slot) {
        auto *b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this, slot);
        editRow->addWidget(b);
    };
    addBtn("+ 10 rows", &DataEntryDialog::addRows);
    addBtn("+ Column", &DataEntryDialog::addColumn);
    addBtn("Delete selected rows", &DataEntryDialog::deleteSelectedRows);
    addBtn("Delete selected columns", &DataEntryDialog::deleteSelectedColumns);
    addBtn("Clear all", &DataEntryDialog::clearAll);
    editRow->addStretch();
    layout->addLayout(editRow);

    auto *bottom = new QHBoxLayout;
    m_saveBtn = new QPushButton("Save");
    m_saveBtn->setEnabled(false);
    m_saveBtn->setToolTip("Write the table back to the file that is shown (open a CSV first)");
    connect(m_saveBtn, &QPushButton::clicked, this, &DataEntryDialog::saveToFile);
    bottom->addWidget(m_saveBtn);
    auto *saveBtn = new QPushButton("Save as CSV...");
    saveBtn->setToolTip("Write the table to a (new) CSV file");
    connect(saveBtn, &QPushButton::clicked, this, &DataEntryDialog::saveCsv);
    bottom->addWidget(saveBtn);
    bottom->addWidget(new QLabel("Separator:"));
    m_sepCombo = new QComboBox;
    m_sepCombo->addItems({"Comma  ( , )", "Semicolon  ( ; )", "Tab", "Pipe  ( | )", "Space"});
    m_sepCombo->setToolTip("Separator used when the table is written to a file");
    bottom->addWidget(m_sepCombo);
    bottom->addStretch();
    auto *plotBtn = new QPushButton("Plot this data");
    plotBtn->setDefault(false);
    plotBtn->setToolTip("Load the table into the plotter (updates the manual dataset if already loaded)");
    connect(plotBtn, &QPushButton::clicked, this, &DataEntryDialog::plotData);
    bottom->addWidget(plotBtn);
    auto *closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    bottom->addWidget(closeBtn);
    layout->addLayout(bottom);

    connect(m_headerCheck, &QCheckBox::toggled, this, &DataEntryDialog::onHeaderToggled);
    updateColumnHeaders();
    updateHeaderRowStyle();
}

void DataEntryDialog::updateColumnHeaders() {
    QStringList labels;
    for (int c = 0; c < m_table->columnCount(); ++c) labels << columnLetters(c);
    m_table->setHorizontalHeaderLabels(labels);
}

// Highlights row 1 while it is being used as the column-name row.
void DataEntryDialog::updateHeaderRowStyle() {
    auto *table = static_cast<PasteTable *>(m_table);
    bool on = m_headerCheck->isChecked();
    for (int c = 0; c < table->columnCount(); ++c) {
        QTableWidgetItem *it = table->ensureItem(0, c);
        QFont f = it->font();
        f.setBold(on);
        it->setFont(f);
        it->setBackground(on ? QBrush(QColor(80, 110, 160, 90)) : QBrush());
    }
    // make sure every newly created cell exists so it can be styled/typed in consistently
    for (int r = 1; r < table->rowCount(); ++r)
        for (int c = 0; c < table->columnCount(); ++c) table->ensureItem(r, c);
}

void DataEntryDialog::onHeaderToggled() { updateHeaderRowStyle(); }

void DataEntryDialog::addRows() {
    m_table->setRowCount(std::min(kMaxRows, m_table->rowCount() + 10));
    updateHeaderRowStyle();
}

void DataEntryDialog::addColumn() {
    if (m_table->columnCount() >= kMaxCols) return;
    m_table->setColumnCount(m_table->columnCount() + 1);
    updateColumnHeaders();
    updateHeaderRowStyle();
}

void DataEntryDialog::deleteSelectedRows() {
    QSet<int> rows;
    for (const auto &rg : m_table->selectedRanges())
        for (int r = rg.topRow(); r <= rg.bottomRow(); ++r) rows.insert(r);
    QList<int> sorted = rows.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int r : sorted) m_table->removeRow(r);
    if (m_table->rowCount() == 0) m_table->setRowCount(1);
    updateHeaderRowStyle();
}

void DataEntryDialog::deleteSelectedColumns() {
    QSet<int> cols;
    for (const auto &rg : m_table->selectedRanges())
        for (int c = rg.leftColumn(); c <= rg.rightColumn(); ++c) cols.insert(c);
    QList<int> sorted = cols.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int c : sorted) m_table->removeColumn(c);
    if (m_table->columnCount() == 0) m_table->setColumnCount(1);
    updateColumnHeaders();
    updateHeaderRowStyle();
}

void DataEntryDialog::clearAll() {
    if (QMessageBox::question(this, "Clear all", "Remove everything from the table?") != QMessageBox::Yes) return;
    setCurrentFile(QString());
    m_cleanedNote.clear();
    m_table->clear();
    m_table->setRowCount(kInitialRows);
    m_table->setColumnCount(kInitialCols);
    updateColumnHeaders();
    updateHeaderRowStyle();
}

QString DataEntryDialog::cellText(int r, int c) const {
    QTableWidgetItem *it = m_table->item(r, c);
    return it ? it->text() : QString();
}

// Finds the bottom-right-most non-empty cell. Returns false if the grid is empty.
bool DataEntryDialog::usedBounds(int &lastRow, int &lastCol) const {
    lastRow = -1; lastCol = -1;
    for (int r = 0; r < m_table->rowCount(); ++r)
        for (int c = 0; c < m_table->columnCount(); ++c)
            if (!cellText(r, c).trimmed().isEmpty()) {
                lastRow = std::max(lastRow, r);
                lastCol = std::max(lastCol, c);
            }
    return lastRow >= 0;
}

// Writes the used part of the grid; the separator comes from the combo box.
bool DataEntryDialog::writeCsv(const QString &path) {
    int lastRow, lastCol;
    if (!usedBounds(lastRow, lastCol)) {
        QMessageBox::information(this, "Nothing to save", "The table is empty.");
        return false;
    }
    const int si = m_sepCombo->currentIndex();
    const QString sep = (si >= 0 && si < kSeps.size()) ? kSeps[si] : QString(",");
    QString out;
    for (int r = 0; r <= lastRow; ++r) {
        QStringList cells;
        for (int c = 0; c <= lastCol; ++c) {
            QString t = cellText(r, c).trimmed();
            if (t.contains(sep) || t.contains('"') || t.contains('\n'))
                t = '"' + t.replace("\"", "\"\"") + '"';
            cells << t;
        }
        while (!cells.isEmpty() && cells.last().isEmpty() && cells.size() > 1) cells.removeLast();   // no trailing separators
        out += cells.join(sep) + "\n";
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, "Couldn't save", "Could not write to:\n" + path);
        return false;
    }
    f.write(m_encoding == "latin-1" ? out.toLatin1() : out.toUtf8());
    f.close();
    m_app->setLastSaveDir(QFileInfo(path).absolutePath());
    return true;
}

void DataEntryDialog::saveCsv() {
    int lastRow, lastCol;
    if (!usedBounds(lastRow, lastCol)) {
        QMessageBox::information(this, "Nothing to save", "The table is empty.");
        return;
    }
    QString startDir = m_app->lastSaveDir().isEmpty() ? QDir::homePath() : m_app->lastSaveDir();
    QString suggested = m_filePath.isEmpty() ? QDir(startDir).filePath("manual_data.csv") : m_filePath;
    QString path = QFileDialog::getSaveFileName(this, "Save table as CSV", suggested,
                                                "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += ".csv";
    if (!writeCsv(path)) return;
    m_cleanedNote.clear();
    setCurrentFile(path, "saved " + QTime::currentTime().toString("HH:mm:ss"));
    offerReload(path);
}

// "Save": back to the file that is shown in the grid.
void DataEntryDialog::saveToFile() {
    if (m_filePath.isEmpty()) { saveCsv(); return; }
    if (!m_cleanedNote.isEmpty() &&
        QMessageBox::question(this, "Overwrite the original file?",
            "The file was cleaned when it was opened (" + m_cleanedNote + ").\n"
            "Saving replaces the original with the cleaned table; the removed lines are lost.\nSave anyway?") != QMessageBox::Yes)
        return;
    if (!writeCsv(m_filePath)) return;
    m_cleanedNote.clear();
    setCurrentFile(m_filePath, "saved " + QTime::currentTime().toString("HH:mm:ss"));
    offerReload(m_filePath);
}

// When the file that was written is also loaded in the plotter, offer to read it again.
void DataEntryDialog::offerReload(const QString &path) {
    if (!m_app->isFileLoaded(path)) return;
    if (QMessageBox::question(this, "Reload in the plotter?",
            QFileInfo(path).fileName() + " is loaded in the plotter. Load the changed file again now?\n"
            "(All loaded files are read again; the plot is cleared.)") == QMessageBox::Yes)
        m_app->reloadLoadedFiles();
}

void DataEntryDialog::setCurrentFile(const QString &path, const QString &note) {
    m_filePath = path;
    m_srcEdited = false;                       // (loadFromDataset sets it again afterwards)
    m_srcBasename.clear();
    m_srcX.clear();
    m_saveBtn->setEnabled(!path.isEmpty());
    m_saveBtn->setToolTip(path.isEmpty() ? "Write the table back to the file that is shown (open a CSV first)"
                                         : "Write the table back to " + path);
    setWindowTitle(path.isEmpty() ? "Enter / edit data" : "Enter / edit data - " + QFileInfo(path).fileName());
    m_fileLabel->setText(path.isEmpty() ? "Typed data (no file)"
                                        : "File: " + path + (note.isEmpty() ? QString() : "   (" + note + ")"));
}

bool DataEntryDialog::confirmDiscard() {
    int r, c;
    if (!usedBounds(r, c)) return true;
    return QMessageBox::question(this, "Replace the table?",
                                 "The table is not empty. Replace its content with the file?") == QMessageBox::Yes;
}

void DataEntryDialog::showEvent(QShowEvent *e) {
    QDialog::showEvent(e);
    refreshLoadedFilesCombo();
}

// Entries: files that are loaded (data = path), data made in the Data editor and the manually entered table
// (data = "<kind>|<basename>|<x column>", found again by name when "Edit" is pressed).
void DataEntryDialog::refreshLoadedFilesCombo() {
    const QString keep = m_loadedCombo->currentData().toString();
    m_loadedCombo->clear();
    for (const auto &ds : m_app->datasets()) {
        if (ds.isFunction) continue;                       // a formula is edited in the Data editor, not as points
        QString key, label;
        if (ds.isEdited) { key = "edited|" + ds.basename + "|" + ds.xColumn; label = ds.basename + "  (Data editor)"; }
        else if (ds.path == manualDataPath()) { key = "manual||"; label = "Manually entered table"; }
        else { key = ds.path; label = ds.basename; }
        m_loadedCombo->addItem(label, key);
        m_loadedCombo->setItemData(m_loadedCombo->count() - 1, ds.isEdited || ds.path == manualDataPath() ? label : ds.path, Qt::ToolTipRole);
    }
    int i = m_loadedCombo->findData(keep);
    if (i >= 0) m_loadedCombo->setCurrentIndex(i);
    if (m_loadedCombo->count() == 0) m_loadedCombo->addItem("(nothing loaded)");
}

void DataEntryDialog::openCsv() {
    QString startDir = m_app->lastOpenDir().isEmpty() ? QDir::homePath() : m_app->lastOpenDir();
    QString path = QFileDialog::getOpenFileName(this, "Open a CSV file", startDir,
                                                "Data files (*.csv *.txt *.tsv *.dat);;All files (*)");
    if (path.isEmpty()) return;
    if (!confirmDiscard()) return;
    m_app->setLastOpenDir(QFileInfo(path).absolutePath());
    loadFromFile(path);
}

void DataEntryDialog::editLoadedFile() {
    const QString key = m_loadedCombo->currentData().toString();
    if (key.isEmpty()) { QMessageBox::information(this, "No data", "Nothing is loaded in the plotter."); return; }
    if (!confirmDiscard()) return;
    if (key.startsWith("edited|") || key.startsWith("manual|")) {
        const QStringList parts = key.split('|');
        const bool edited = parts[0] == "edited";
        for (const auto &ds : m_app->datasets()) {
            if (ds.isFunction) continue;
            if (edited ? (ds.isEdited && ds.basename == parts.value(1) && ds.xColumn == parts.value(2))
                       : ds.path == manualDataPath()) {
                loadFromDataset(ds);
                return;
            }
        }
        QMessageBox::information(this, "Not found", "That data is no longer loaded.");
        return;
    }
    loadFromFile(key);
}

// Shows data that exists only in the plotter (made in the Data editor, or typed) in the table. It has no file;
// "Plot this data" puts the changed table back in place of it.
void DataEntryDialog::loadFromDataset(const Dataset &ds) {
    QVector<QStringList> rows;
    QStringList names;
    int n = 0;
    for (const auto &col : ds.columns) { names << col.name; n = std::max(n, int(col.values.size())); }
    rows << names;
    for (int i = 0; i < n; ++i) {
        QStringList row;
        for (const auto &col : ds.columns) {
            if (!col.isNumeric && i < col.labels.size()) row << col.labels[i];
            else if (i < col.values.size() && std::isfinite(col.values[i])) row << QString::number(col.values[i], 'g', 12);
            else row << QString();
        }
        rows << row;
    }
    fillTable(rows, true);
    m_cleanedNote.clear();
    setCurrentFile(QString());
    m_srcEdited = ds.isEdited;
    m_srcBasename = ds.basename;
    m_srcX = ds.xColumn;
    m_fileLabel->setText(ds.isEdited ? "Data from the Data editor: " + ds.basename + "   (Plot this data replaces it)"
                                     : "The manually entered table   (Plot this data replaces it)");
}

// Shows the file as it is: every cell as text (so nothing is changed by number parsing), every line, also comments.
void DataEntryDialog::loadFromFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Couldn't open", "Could not read:\n" + path);
        return;
    }
    QByteArray raw = f.readAll();
    f.close();
    if (raw.startsWith("\xEF\xBB\xBF")) raw = raw.mid(3);
    QString text = QString::fromUtf8(raw);
    m_encoding = "utf-8";
    if (text.contains(QChar(0xFFFD))) { text = QString::fromLatin1(raw); m_encoding = "latin-1"; }

    QStringList sample;
    for (const QString &line : text.split(QRegularExpression("\r\n|\n|\r"))) {
        if (!line.trimmed().isEmpty() && !line.trimmed().startsWith('#')) sample << line;
        if (sample.size() >= 300) break;
    }
    const QString sep = sample.isEmpty() ? QString(",") : Detect::detectSeparator(sample);
    const QVector<QStringList> rowsIn = parseFileText(text, sep);
    if (rowsIn.isEmpty()) { QMessageBox::information(this, "Empty file", "The file has no content."); return; }

    QVector<QStringList> rows = rowsIn;
    CleanStats st;
    const bool cleaned = m_cleanCheck->isChecked();
    if (cleaned) cleanRows(rows, st);
    if (rows.isEmpty()) { QMessageBox::information(this, "No data", "Nothing is left after the clean-up. Untick \"Clean up when opening\" to see the file as it is."); return; }

    int ncols = 0;
    for (const auto &r : rows) ncols = std::max(ncols, int(r.size()));
    bool truncated = false;
    if (rows.size() > kMaxRows) { rows.resize(kMaxRows); truncated = true; }
    if (ncols > kMaxCols) { ncols = kMaxCols; truncated = true; }
    if (truncated)
        QMessageBox::warning(this, "Large file",
            QString("The file is larger than the table can hold; only the first %1 rows and %2 columns are shown.\n"
                    "Saving would cut the rest off, so do not save over the original file.").arg(kMaxRows).arg(kMaxCols));

    bool header = st.header;
    if (!cleaned) {                                          // does the first data row look like column names?
        auto dataRow = [&](int from) { for (int r = from; r < rows.size(); ++r) if (!rows[r].isEmpty() && !rows[r][0].trimmed().startsWith('#')) return r; return -1; };
        const int r0 = dataRow(0), r1 = r0 >= 0 ? dataRow(r0 + 1) : -1;
        if (r0 >= 0) {
            bool nonNumeric0 = false, allNumeric1 = r1 >= 0;
            for (const QString &t : rows[r0]) if (!t.trimmed().isEmpty() && !Detect::isNumericToken(t.trimmed())) nonNumeric0 = true;
            if (r1 >= 0) for (const QString &t : rows[r1]) if (!t.trimmed().isEmpty() && !Detect::isNumericToken(t.trimmed())) allNumeric1 = false;
            header = nonNumeric0 && allNumeric1;
        }
    }
    fillTable(rows, header);

    int si = kSeps.indexOf(sep == "\\s+" ? QString(" ") : sep);
    if (si >= 0) m_sepCombo->setCurrentIndex(si);
    m_cleanedNote.clear();
    if (cleaned && (st.removedRows > 0 || st.removedCols > 0))
        m_cleanedNote = QString("cleaned: %1 line(s) and %2 empty column(s) removed").arg(st.removedRows).arg(st.removedCols);
    setCurrentFile(path, QString("%1 rows, %2 columns").arg(rows.size()).arg(ncols) +
                         (m_cleanedNote.isEmpty() ? QString() : ", " + m_cleanedNote));
}

// Puts rows of text into the grid.
void DataEntryDialog::fillTable(const QVector<QStringList> &rows, bool header) {
    int ncols = 0;
    for (const auto &r : rows) ncols = std::max(ncols, int(r.size()));
    ncols = std::min(ncols, kMaxCols);
    const int nrows = std::min(int(rows.size()), kMaxRows);
    auto *table = static_cast<PasteTable *>(m_table);
    table->setUpdatesEnabled(false);
    table->clear();
    table->setRowCount(std::max(nrows, kInitialRows));
    table->setColumnCount(std::max(ncols, kInitialCols));
    for (int r = 0; r < nrows; ++r)
        for (int c = 0; c < rows[r].size() && c < ncols; ++c) table->ensureItem(r, c)->setText(rows[r][c]);
    m_headerCheck->blockSignals(true);
    m_headerCheck->setChecked(header);
    m_headerCheck->blockSignals(false);
    updateColumnHeaders();
    updateHeaderRowStyle();
    table->setUpdatesEnabled(true);
    table->scrollToTop();
    table->setCurrentCell(0, 0);
}

// "Clean up" button: the same clean-up for what is in the table now.
void DataEntryDialog::cleanTable() {
    int lastRow, lastCol;
    if (!usedBounds(lastRow, lastCol)) return;
    QVector<QStringList> rows;
    for (int r = 0; r <= lastRow; ++r) {
        QStringList row;
        for (int c = 0; c <= lastCol; ++c) row << cellText(r, c);
        rows << row;
    }
    CleanStats st;
    cleanRows(rows, st);
    if (rows.isEmpty()) { QMessageBox::information(this, "No data", "Nothing is left after the clean-up."); return; }
    if (st.removedRows == 0 && st.removedCols == 0) { QMessageBox::information(this, "Clean up", "The table is already clean."); return; }
    fillTable(rows, st.header);
    m_cleanedNote = QString("cleaned: %1 line(s) and %2 empty column(s) removed").arg(st.removedRows).arg(st.removedCols);
    if (!m_filePath.isEmpty()) setCurrentFile(m_filePath, m_cleanedNote);
    else m_fileLabel->setText(m_fileLabel->text() + "   (" + m_cleanedNote + ")");   // (data of the plotter keeps its source)
}

Dataset DataEntryDialog::buildDataset(QString &error) const {
    Dataset ds;
    int lastRow, lastCol;
    if (!usedBounds(lastRow, lastCol)) { error = "The table is empty. Type or paste some data first."; return ds; }
    bool hasHeader = m_headerCheck->isChecked();
    int firstData = hasHeader ? 1 : 0;
    int ncols = lastCol + 1;

    QStringList names;
    for (int c = 0; c < ncols; ++c) {
        QString n = hasHeader ? cellText(0, c).trimmed() : QString();
        if (n.isEmpty()) n = QString("Col%1").arg(c + 1);
        QString base = n; int k = 2;
        while (names.contains(n)) n = QString("%1_%2").arg(base).arg(k++); // names must be unique
        names << n;
    }

    ds.columns.resize(ncols);
    for (int c = 0; c < ncols; ++c) ds.columns[c].name = names[c];

    int rows = 0;
    QVector<QStringList> rawText(ncols);
    for (int r = firstData; r <= lastRow; ++r) {
        bool any = false;
        for (int c = 0; c < ncols; ++c)
            if (!cellText(r, c).trimmed().isEmpty()) { any = true; break; }
        if (!any) continue; // skip blank rows
        for (int c = 0; c < ncols; ++c) {
            ds.columns[c].values.push_back(parseCell(cellText(r, c)));
            rawText[c] << cellText(r, c).trimmed();
        }
        ++rows;
    }
    if (rows == 0) {
        error = hasHeader ? "Add at least one row of data below the column names."
                          : "There is no data to plot.";
        return ds;
    }

    for (auto &col : ds.columns) {
        int nonNan = 0;
        for (double v : col.values) if (!std::isnan(v)) ++nonNan;
        col.isNumeric = (double(nonNan) / col.values.size()) >= 0.8;
    }
    for (int c = 0; c < ncols; ++c)
        if (!ds.columns[c].isNumeric) ds.columns[c].labels = rawText[c];   // category labels for bar charts

    ds.path = manualDataPath();
    ds.basename = "manual_data.csv";
    ds.info.sep = ",";
    ds.info.decimal = ".";
    ds.info.hasHeader = hasHeader;
    ds.info.encoding = "utf-8";
    ds.info.rows = rows;
    ds.info.cols = ncols;
    ds.info.skippedRows = 0;
    ds.xColumn = ds.columns.first().name;
    return ds;
}

void DataEntryDialog::plotData() {
    QString error;
    Dataset ds = buildDataset(error);
    if (!error.isEmpty()) {
        QMessageBox::information(this, "Can't plot yet", error);
        return;
    }
    if (m_srcEdited) {                                  // came from the Data editor: replace that data
        ds.isEdited = true;
        ds.basename = m_srcBasename;
        if (ds.columnIndex(m_srcX) >= 0) ds.xColumn = m_srcX;
        emit editedDatasetReady(ds);
        return;
    }
    emit datasetReady(ds);
}
