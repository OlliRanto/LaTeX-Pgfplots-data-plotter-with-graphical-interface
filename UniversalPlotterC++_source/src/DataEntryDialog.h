#pragma once
#include <QDialog>
#include "Dataset.h"
#include <QVector>
#include <QStringList>

class QTableWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QShowEvent;
class MainWindow;

// "Enter / edit data" window: an Excel-like grid that accepts typed values and
// Ctrl+V pastes straight from Excel / LibreOffice / any tab-separated source.
// A CSV file (any of the loaded files, or one picked with "Open CSV...") can be shown in the grid, edited
// and written back ("Save" / "Save as CSV..."). The grid can be handed to the plotter as a Dataset ("Plot this data").
class DataEntryDialog : public QDialog {
    Q_OBJECT
public:
    explicit DataEntryDialog(MainWindow *app);

signals:
    // Emitted when the user presses "Plot this data".
    void datasetReady(const Dataset &ds);
    // Same, when the table was opened from data of the Data editor: that data is replaced.
    void editedDatasetReady(const Dataset &ds);

protected:
    void showEvent(QShowEvent *e) override;

private slots:
    void openCsv();
    void editLoadedFile();
    void saveToFile();
    void cleanTable();
    void addRows();
    void addColumn();
    void deleteSelectedRows();
    void deleteSelectedColumns();
    void clearAll();
    void saveCsv();
    void plotData();
    void onHeaderToggled();

private:
    void loadFromFile(const QString &path);
    void loadFromDataset(const Dataset &ds);
    void fillTable(const QVector<QStringList> &rows, bool header);
    bool writeCsv(const QString &path);
    void setCurrentFile(const QString &path, const QString &note = QString());
    void offerReload(const QString &path);
    void refreshLoadedFilesCombo();
    bool confirmDiscard();                              // true: the grid may be overwritten
    void updateColumnHeaders();
    void updateHeaderRowStyle();
    bool usedBounds(int &lastRow, int &lastCol) const; // false if the grid is empty
    QString cellText(int r, int c) const;
    Dataset buildDataset(QString &error) const;         // error non-empty on failure

    MainWindow *m_app;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_headerCheck = nullptr;
    QComboBox *m_sepCombo = nullptr;
    QComboBox *m_loadedCombo = nullptr;     // the files loaded in the plotter
    QLabel *m_fileLabel = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QCheckBox *m_cleanCheck = nullptr;       // clean up files when opening them
    QString m_cleanedNote;                   // what the clean-up of the shown file removed (empty: nothing / saved)
    bool m_srcEdited = false;                // the table shows data of the Data editor ...
    QString m_srcBasename, m_srcX;           // ... with this name and X column
    QString m_filePath;                      // file shown in the grid ("Save" writes it); empty = typed data
    QString m_encoding = "utf-8";
};
