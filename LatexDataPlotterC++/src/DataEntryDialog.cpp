#include "DataEntryDialog.h"
#include "MainWindow.h"

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
#include <cmath>
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
QVector<QStringList> parseClipboard(const QString &textIn) {
    QString text = textIn;
    text.replace("\r\n", "\n").replace('\r', '\n');
    QChar delim = QChar('\t');
    if (!text.contains('\t')) delim = text.contains(';') ? QChar(';') : QChar(0);

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
    setWindowTitle("Enter data manually");
    resize(760, 560);

    auto *layout = new QVBoxLayout(this);

    auto *hint = new QLabel(
        "Type values or paste straight from Excel (click a cell, then Ctrl+V). "
        "Ctrl+C / Ctrl+X / Delete work on the selected cells. Decimal commas are understood.");
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
    auto *saveBtn = new QPushButton("Save as CSV...");
    saveBtn->setToolTip("Optional: write the table to a CSV file");
    connect(saveBtn, &QPushButton::clicked, this, &DataEntryDialog::saveCsv);
    bottom->addWidget(saveBtn);
    bottom->addWidget(new QLabel("Separator:"));
    m_sepCombo = new QComboBox;
    m_sepCombo->addItems({"Comma  ( , )", "Semicolon  ( ; )", "Tab"});
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

void DataEntryDialog::saveCsv() {
    int lastRow, lastCol;
    if (!usedBounds(lastRow, lastCol)) {
        QMessageBox::information(this, "Nothing to save", "The table is empty.");
        return;
    }
    QString startDir = m_app->lastSaveDir().isEmpty() ? QDir::homePath() : m_app->lastSaveDir();
    QString path = QFileDialog::getSaveFileName(this, "Save table as CSV",
                                                QDir(startDir).filePath("manual_data.csv"),
                                                "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += ".csv";

    QString sep = m_sepCombo->currentIndex() == 1 ? ";" : m_sepCombo->currentIndex() == 2 ? "\t" : ",";
    QString out;
    for (int r = 0; r <= lastRow; ++r) {
        QStringList cells;
        for (int c = 0; c <= lastCol; ++c) {
            QString t = cellText(r, c).trimmed();
            if (t.contains(sep) || t.contains('"') || t.contains('\n'))
                t = '"' + t.replace("\"", "\"\"") + '"';
            cells << t;
        }
        out += cells.join(sep) + "\n";
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, "Couldn't save", "Could not write to:\n" + path);
        return;
    }
    f.write(out.toUtf8());
    f.close();
    m_app->setLastSaveDir(QFileInfo(path).absolutePath());
    QMessageBox::information(this, "Saved", "CSV written to:\n" + path);
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
    emit datasetReady(ds);
}
