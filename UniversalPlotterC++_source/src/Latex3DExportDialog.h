#pragma once
#include <QDialog>
#include <QMap>
#include <QVector>
#include <QStringList>
#include "Plot3DWidget.h"

class MainWindow;
class QRadioButton;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QWidget;
class QLabel;
class QPushButton;

// LaTeX (pgfplots) export of the 3D plot. Same workflow as the 2D export: choose where the data
// comes from (the formula itself for plots made from an expression, data points embedded in the code,
// or CSV file(s) next to the .tex), look at the generated code,
// then copy it, save the .tex (+ CSV), compile it to PDF or convert it to SVG.
class Latex3DExportDialog : public QDialog {
    Q_OBJECT
public:
    Latex3DExportDialog(MainWindow *app, Plot3DWidget *plot, QWidget *parent = nullptr);

private:
    struct ProcStep { QString program; QStringList args; };

    Plot3DLatexOptions collect() const;
    void loadSettings();
    void saveSettings() const;
    void generate();
    void showCodeWindow(const QString &code, const QString &svgCode, const QMap<QString, QString> &csvFiles,
                        const QMap<QString, QString> &svgCsvFiles);
    // Writes figure.tex (+ CSV) to a temporary folder, runs the steps there and offers the result for saving.
    void runBuild(QDialog *win, QLabel *status, const QVector<QPushButton *> &buttons, const QString &tex,
                  const QMap<QString, QString> &csvFiles, const QVector<ProcStep> &steps, const QString &route,
                  const QString &outFile, const QString &what);

    MainWindow *m_app;
    Plot3DWidget *m_plot;
    QRadioButton *m_formulaRadio = nullptr, *m_embedRadio = nullptr, *m_csvRadio = nullptr;
    QWidget *m_csvFrame = nullptr, *m_formulaFrame = nullptr;
    QSpinBox *m_samplesSpin = nullptr;
    QLineEdit *m_csvNameEdit = nullptr, *m_captionEdit = nullptr, *m_figLabelEdit = nullptr;
    QComboBox *m_placementCombo = nullptr;
    QComboBox *m_sepCombo = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QCheckBox *m_wrapCheck = nullptr, *m_escapeCheck = nullptr;
};
