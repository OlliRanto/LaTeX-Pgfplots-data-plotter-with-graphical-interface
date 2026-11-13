#pragma once
#include <QDialog>
#include "Dataset.h"

class QTableWidget;
class QCheckBox;
class QComboBox;
class MainWindow;

// "Enter data manually" window: an Excel-like grid that accepts typed values and
// Ctrl+V pastes straight from Excel / LibreOffice / any tab-separated source.
// The grid can be handed to the plotter as a Dataset ("Plot this data") and,
// optionally, saved to a CSV file ("Save as CSV...").
class DataEntryDialog : public QDialog {
    Q_OBJECT
public:
    explicit DataEntryDialog(MainWindow *app);

signals:
    // Emitted when the user presses "Plot this data".
    void datasetReady(const Dataset &ds);

private slots:
    void addRows();
    void addColumn();
    void deleteSelectedRows();
    void deleteSelectedColumns();
    void clearAll();
    void saveCsv();
    void plotData();
    void onHeaderToggled();

private:
    void updateColumnHeaders();
    void updateHeaderRowStyle();
    bool usedBounds(int &lastRow, int &lastCol) const; // false if the grid is empty
    QString cellText(int r, int c) const;
    Dataset buildDataset(QString &error) const;         // error non-empty on failure

    MainWindow *m_app;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_headerCheck = nullptr;
    QComboBox *m_sepCombo = nullptr;
};
