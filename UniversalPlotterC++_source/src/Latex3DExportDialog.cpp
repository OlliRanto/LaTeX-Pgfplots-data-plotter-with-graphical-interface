#include "Latex3DExportDialog.h"
#include "MainWindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QTimer>
#include <QSettings>
#include <algorithm>
#include <functional>
#include <memory>

namespace {

QString findLatexEngine() {
    static const QStringList candidates = { "lualatex", "latexmk", "pdflatex", "xelatex" };
    for (const QString &c : candidates)
        if (!QStandardPaths::findExecutable(c).isEmpty()) return c;
    return QString();
}

void showLogWindow(QWidget *parent, const QString &log) {
    auto *dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString::fromUtf8("Compilation failed \u2014 log"));
    dlg->resize(700, 500);
    auto *layout = new QVBoxLayout(dlg);
    layout->addWidget(new QLabel("The build failed. Full log below (scroll for details):"));
    auto *logEdit = new QTextEdit;
    logEdit->setReadOnly(true);
    logEdit->setFontFamily("monospace");
    logEdit->setPlainText(log);
    layout->addWidget(logEdit, 1);
    auto *row = new QHBoxLayout;
    auto *copyBtn = new QPushButton("Copy log");
    QObject::connect(copyBtn, &QPushButton::clicked, dlg, [log]() { QGuiApplication::clipboard()->setText(log); });
    row->addWidget(copyBtn);
    auto *closeBtn = new QPushButton("Close");
    QObject::connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
    row->addWidget(closeBtn);
    layout->addLayout(row);
    dlg->show();
}

// Runs external programs one after another in workDir (2 minutes each) and reports once.
template <typename Step>
void runProcessChain(QObject *owner, const QString &workDir, const QVector<Step> &steps,
                     std::function<void(bool ok, bool timedOut, const QString &log)> done) {
    auto idx = std::make_shared<int>(0);
    auto log = std::make_shared<QString>();
    auto run = std::make_shared<std::function<void()>>();
    *run = [=]() {
        if (*idx >= steps.size()) { done(true, false, *log); return; }
        const Step st = steps[*idx];
        auto *proc = new QProcess(owner);
        proc->setWorkingDirectory(workDir);
        proc->setProgram(st.program);
        proc->setArguments(st.args);
        auto timedOut = std::make_shared<bool>(false);
        auto *timer = new QTimer(proc);
        timer->setSingleShot(true);
        timer->setInterval(120000);
        QObject::connect(timer, &QTimer::timeout, proc, [proc, timedOut]() { *timedOut = true; proc->kill(); });
        QObject::connect(proc, &QProcess::errorOccurred, owner, [=](QProcess::ProcessError err) {
            if (err != QProcess::FailedToStart) return;
            *log += QString("Could not start '%1'. Make sure it is installed and on the system PATH.\n").arg(st.program);
            proc->deleteLater();
            done(false, false, *log);
        });
        QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), owner,
                         [=](int exitCode, QProcess::ExitStatus status) {
            timer->stop();
            *log += QString("$ %1 %2\n").arg(st.program, st.args.join(' '))
                  + QString::fromLocal8Bit(proc->readAllStandardOutput()) + "\n"
                  + QString::fromLocal8Bit(proc->readAllStandardError()) + "\n";
            if (status == QProcess::CrashExit)
                *log += QString("[%1 crashed (it was killed by the system, e.g. a stack overflow)]\n").arg(st.program);
            else if (exitCode != 0)
                *log += QString("[%1 exited with code %2]\n").arg(st.program).arg(exitCode);
            proc->deleteLater();
            if (status == QProcess::NormalExit && exitCode == 0) { ++*idx; (*run)(); }
            else done(false, *timedOut, *log);
        });
        timer->start();
        proc->start();
    };
    (*run)();
}

} // namespace

// ----------------------------------------------------------------- dialog
Latex3DExportDialog::Latex3DExportDialog(MainWindow *app, Plot3DWidget *plot, QWidget *parent)
    : QDialog(parent), m_app(app), m_plot(plot) {
    setWindowTitle("Export 3D plot to LaTeX (pgfplots)");
    auto *outer = new QVBoxLayout(this);

    // ---- data source
    auto *srcBox = new QGroupBox("Data source");
    auto *srcLay = new QVBoxLayout(srcBox);
    // Plots made from an expression can be written as the expression itself (\\addplot3 {f(x,y)}), like the
    // function plots of the 2D export; pgfplots then calculates the points when the document is compiled.
    const bool canFormula = m_plot->hasFormula();
    m_formulaRadio = new QRadioButton(m_plot->formulaIsSurface() || !canFormula
                                          ? "Math expression  (\\addplot3 { f(x, y) })"
                                          : "Math expression  (\\addplot3 ( x(t), y(t), z(t) ))");
    m_formulaRadio->setEnabled(canFormula);
    m_formulaRadio->setToolTip(canFormula
        ? "Writes the formula instead of data points: short code, no CSV file, and the curve is exact at any size.\n"
          "pgfplots evaluates it while compiling."
        : "Only available for plots made from an expression (z = f(x, y) or a curve x(t), y(t), z(t)).");
    m_embedRadio = new QRadioButton("Embed data points directly in the code");
    m_embedRadio->setChecked(true);
    m_embedRadio->setToolTip("Self-contained .tex file. Very large grids and lines are thinned so that TeX does not run out of memory.");
    m_csvRadio = new QRadioButton(QString::fromUtf8("Load from CSV file(s) (\\addplot3 table)"));
    m_csvRadio->setToolTip("The complete data is written to CSV file(s) next to the .tex file (nothing is thinned).");
    srcLay->addWidget(m_formulaRadio);

    m_formulaFrame = new QWidget;
    auto *fLay = new QHBoxLayout(m_formulaFrame);
    fLay->setContentsMargins(16, 0, 0, 4);
    fLay->addWidget(new QLabel(m_plot->formulaIsSurface() ? "Samples per axis:" : "Samples:"));
    m_samplesSpin = new QSpinBox;
    m_samplesSpin->setRange(2, 300);
    m_samplesSpin->setValue(std::min(std::max(m_plot->formulaSamples(), 2), 300));
    m_samplesSpin->setToolTip("How many points pgfplots calculates the formula at.\n"
                              "The SVG export is limited to 60 per axis (surface) / 2000 (curve) so that dvisvgm copes.");
    fLay->addWidget(m_samplesSpin);
    fLay->addStretch();
    srcLay->addWidget(m_formulaFrame);
    m_formulaFrame->setVisible(false);
    connect(m_formulaRadio, &QRadioButton::toggled, this, [this](bool on) { m_formulaFrame->setVisible(on); adjustSize(); });

    srcLay->addWidget(m_embedRadio);
    srcLay->addWidget(m_csvRadio);

    m_csvFrame = new QWidget;
    auto *csvLay = new QVBoxLayout(m_csvFrame);
    csvLay->setContentsMargins(16, 4, 0, 0);
    auto *intro = new QLabel("The surface, line and points of the plot are each written to their own CSV file "
                             "(<name>_surface.csv, <name>_line.csv, <name>_points.csv, only those that exist), "
                             "saved next to the .tex file. The columns are x, y, z with a header row.");
    intro->setWordWrap(true);
    intro->setStyleSheet("color:#555;");
    csvLay->addWidget(intro);
    auto *csvRow = new QHBoxLayout;
    csvRow->addWidget(new QLabel("File name:"));
    m_csvNameEdit = new QLineEdit("plot3d");
    m_csvNameEdit->setMaximumWidth(180);
    csvRow->addWidget(m_csvNameEdit);
    csvRow->addWidget(new QLabel("Col sep:"));
    m_sepCombo = new QComboBox;
    m_sepCombo->addItems({"comma", "semicolon", "tab", "space"});
    csvRow->addWidget(m_sepCombo);
    csvRow->addStretch();
    csvLay->addLayout(csvRow);
    srcLay->addWidget(m_csvFrame);
    m_csvFrame->setVisible(false);
    connect(m_csvRadio, &QRadioButton::toggled, this, [this](bool on) { m_csvFrame->setVisible(on); adjustSize(); });
    outer->addWidget(srcBox);

    // ---- figure
    auto *figBox = new QGroupBox("Figure");
    auto *figLay = new QHBoxLayout(figBox);
    figLay->addWidget(new QLabel("Width (cm):"));
    m_widthSpin = new QDoubleSpinBox;
    m_widthSpin->setRange(3, 40);
    m_widthSpin->setSingleStep(0.5);
    m_widthSpin->setValue(11);
    figLay->addWidget(m_widthSpin);
    figLay->addStretch();
    outer->addWidget(figBox);

    // ---- figure wrapper: the same controls as in the other plot types' export
    auto *wrapBox = new QGroupBox("Figure wrapper (optional)");
    auto *grid = new QGridLayout(wrapBox);
    m_wrapCheck = new QCheckBox(QString::fromUtf8("Wrap the plot in a \\begin{figure} ... \\end{figure} environment"));
    m_wrapCheck->setToolTip("Off: a standalone document that compiles to a tightly cropped figure.\n"
                            "SVG export always uses the standalone form.");
    grid->addWidget(m_wrapCheck, 0, 0, 1, 4);
    grid->addWidget(new QLabel("Caption:"), 1, 0);
    m_captionEdit = new QLineEdit;
    grid->addWidget(m_captionEdit, 1, 1, 1, 3);
    grid->addWidget(new QLabel("Label:"), 2, 0);
    m_figLabelEdit = new QLineEdit("fig:");
    grid->addWidget(m_figLabelEdit, 2, 1);
    grid->addWidget(new QLabel("Placement:"), 2, 2);
    m_placementCombo = new QComboBox;
    m_placementCombo->addItems({"htbp", "ht", "h", "t", "b", "p", "H"});
    m_placementCombo->setMaximumWidth(90);
    grid->addWidget(m_placementCombo, 2, 3);
    auto setWrapEnabled = [this](bool on) {
        m_captionEdit->setEnabled(on);
        m_figLabelEdit->setEnabled(on);
        m_placementCombo->setEnabled(on);
    };
    setWrapEnabled(false);
    connect(m_wrapCheck, &QCheckBox::toggled, this, setWrapEnabled);
    outer->addWidget(wrapBox);

    // ---- buttons
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *genBtn = new QPushButton("Generate LaTeX...");
    genBtn->setDefault(true);
    auto *cancelBtn = new QPushButton("Cancel");
    btnRow->addWidget(genBtn);
    btnRow->addWidget(cancelBtn);
    outer->addLayout(btnRow);
    connect(genBtn, &QPushButton::clicked, this, &Latex3DExportDialog::generate);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    loadSettings();
    resize(520, sizeHint().height());
}

Plot3DLatexOptions Latex3DExportDialog::collect() const {
    Plot3DLatexOptions o;
    o.useFormula = m_formulaRadio->isChecked();
    o.formulaSamples = m_samplesSpin->value();
    o.useCsv = m_csvRadio->isChecked();
    o.csvBaseName = m_csvNameEdit->text().trimmed();
    o.colSep = m_sepCombo->currentText();
    o.widthCm = m_widthSpin->value();
    o.wrapFigure = m_wrapCheck->isChecked();
    o.caption = m_captionEdit->text().trimmed();
    o.figLabel = m_figLabelEdit->text().trimmed();
    o.figurePlacement = m_placementCombo->currentText();
    return o;
}

void Latex3DExportDialog::saveSettings() const {
    QSettings s;
    s.beginGroup("latex3d");
    s.setValue("source", m_formulaRadio->isChecked() ? "formula" : m_csvRadio->isChecked() ? "csv" : "embed");
    s.setValue("csvName", m_csvNameEdit->text());
    s.setValue("colSep", m_sepCombo->currentText());
    s.setValue("width", m_widthSpin->value());
    s.setValue("wrap", m_wrapCheck->isChecked());
    s.setValue("caption", m_captionEdit->text());
    s.setValue("figLabel", m_figLabelEdit->text());
    s.setValue("placement", m_placementCombo->currentText());
    s.endGroup();
}

void Latex3DExportDialog::loadSettings() {
    QSettings s;
    s.beginGroup("latex3d");
    // an expression plot defaults to its formula; later the last choice is remembered
    const QString legacy = s.value("useCsv", false).toBool() ? "csv" : "embed";
    QString source = s.value("source", m_formulaRadio->isEnabled() ? "formula" : legacy).toString();
    if (source == "formula" && !m_formulaRadio->isEnabled()) source = "embed";
    if (source == "formula") m_formulaRadio->setChecked(true);
    else if (source == "csv") m_csvRadio->setChecked(true);
    else m_embedRadio->setChecked(true);
    m_csvNameEdit->setText(s.value("csvName", "plot3d").toString());
    const int si = m_sepCombo->findText(s.value("colSep", "comma").toString());
    if (si >= 0) m_sepCombo->setCurrentIndex(si);
    m_widthSpin->setValue(s.value("width", 11.0).toDouble());
    m_wrapCheck->setChecked(s.value("wrap", false).toBool());
    m_captionEdit->setText(s.value("caption").toString());
    m_figLabelEdit->setText(s.value("figLabel", "fig:").toString());
    const int pi = m_placementCombo->findText(s.value("placement", "htbp").toString());
    if (pi >= 0) m_placementCombo->setCurrentIndex(pi);
    s.endGroup();
}

void Latex3DExportDialog::generate() {
    Plot3DLatexOptions opts = collect();
    if (opts.useCsv && opts.csvBaseName.isEmpty()) {
        QMessageBox::critical(this, "Invalid input", "Enter a file name for the CSV file(s).");
        return;
    }
    saveSettings();
    const Plot3DLatexResult full = m_plot->toPgfplots(opts);
    Plot3DLatexOptions svgOpts = opts;
    svgOpts.wrapFigure = false;                       // SVG is a single figure: no page, margins or caption
    svgOpts.forSvg = true;                            // shallow SVG nesting + data limited so that dvisvgm and pdfTeX cope
    const Plot3DLatexResult svg = m_plot->toPgfplots(svgOpts);
    showCodeWindow(full.code, svg.code, full.csvFiles, svg.csvFiles);
}

void Latex3DExportDialog::showCodeWindow(const QString &code, const QString &svgCode, const QMap<QString, QString> &csvFiles,
                                         const QMap<QString, QString> &svgCsvFiles) {
    auto *win = new QDialog(this);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->setWindowTitle("LaTeX / pgfplots code (3D plot)");
    win->resize(720, 640);
    auto *layout = new QVBoxLayout(win);

    auto *text = new QTextEdit;
    text->setReadOnly(true);
    text->setFontFamily("monospace");
    text->setPlainText(code);
    layout->addWidget(text, 1);

    if (!csvFiles.isEmpty()) {
        auto *note = new QLabel(QString("This export also needs %1 accompanying CSV file(s), saved alongside the .tex file: %2")
                                    .arg(csvFiles.size()).arg(QStringList(csvFiles.keys()).join(", ")));
        note->setWordWrap(true);
        note->setStyleSheet("color:#555;");
        layout->addWidget(note);
    }

    auto *row = new QHBoxLayout;
    auto *copyBtn = new QPushButton("Copy to clipboard");
    connect(copyBtn, &QPushButton::clicked, win, [code]() { QGuiApplication::clipboard()->setText(code); });
    row->addWidget(copyBtn);

    auto *saveBtn = new QPushButton(QString("Save .tex") + (csvFiles.isEmpty() ? "..." : " (+ CSV) ..."));
    connect(saveBtn, &QPushButton::clicked, win, [this, win, code, csvFiles]() {
        const QString startDir = m_app->lastSaveDir().isEmpty() ? QDir::homePath() : m_app->lastSaveDir();
        QString path = QFileDialog::getSaveFileName(win, "Save LaTeX file", startDir + "/plot3d.tex", "LaTeX files (*.tex)");
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += ".tex";
        m_app->setLastSaveDir(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(win, "Save failed", "Could not write to that location.");
            return;
        }
        QTextStream(&f) << code;
        f.close();
        const QDir dir = QFileInfo(path).absoluteDir();
        for (auto it = csvFiles.constBegin(); it != csvFiles.constEnd(); ++it) {
            QFile cf(dir.filePath(it.key()));
            if (!cf.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QMessageBox::critical(win, "Save failed", "Could not write " + it.key());
                return;
            }
            QTextStream(&cf) << it.value();
        }
        QMessageBox::information(win, "Saved", "LaTeX (and any CSV data) written to:\n" + QFileInfo(path).absolutePath());
    });
    row->addWidget(saveBtn);

    auto *compileBtn = new QPushButton("Compile to PDF...");
    row->addWidget(compileBtn);
    auto *svgBtn = new QPushButton("Save as SVG...");
    svgBtn->setToolTip("Vector SVG straight from LaTeX (latex -> dvisvgm), no PDF needed");
    row->addWidget(svgBtn);
    auto *status = new QLabel;
    status->setStyleSheet("color:#555;");
    row->addWidget(status);

    // ---- PDF
    connect(compileBtn, &QPushButton::clicked, win, [=]() {
        const QString engine = findLatexEngine();
        if (engine.isEmpty()) {
            QMessageBox::critical(win, "No LaTeX installation found",
                "Couldn't find lualatex, latexmk, pdflatex, or xelatex on your system PATH.\n\n"
                "Install a LaTeX distribution (e.g. TeX Live, MiKTeX, or MacTeX) to compile here, "
                "or use \"Save .tex\" and compile it elsewhere (e.g. Overleaf).");
            return;
        }
        QVector<ProcStep> steps;
        if (engine == "latexmk") steps.push_back({ "latexmk", { "-lualatex", "-interaction=nonstopmode", "-halt-on-error", "figure.tex" } });
        else steps.push_back({ engine, { "-interaction=nonstopmode", "-halt-on-error", "figure.tex" } });
        runBuild(win, status, { compileBtn, svgBtn }, code, csvFiles, steps, engine, "figure.pdf", "PDF");
    });

    // ---- SVG: latex -> dvisvgm needs no PDF; otherwise PDF -> SVG with pdf2svg or Inkscape
    connect(svgBtn, &QPushButton::clicked, win, [=]() {
        QVector<ProcStep> steps;
        QString route;
        if (!QStandardPaths::findExecutable("latex").isEmpty() && !QStandardPaths::findExecutable("dvisvgm").isEmpty()) {
            route = "latex -> dvisvgm";
            steps = {
                { "latex", { "-interaction=nonstopmode", "-halt-on-error", "-jobname=figure",
                             "\\def\\pgfsysdriver{pgfsys-dvisvgm.def}\\input{figure.tex}" } },
                { "dvisvgm", { "--no-fonts", "--exact-bbox", "figure.dvi", "-o", "figure.svg" } }
            };
        } else {
            const QString engine = findLatexEngine();
            const QString converter = !QStandardPaths::findExecutable("pdf2svg").isEmpty() ? "pdf2svg"
                                    : !QStandardPaths::findExecutable("inkscape").isEmpty() ? "inkscape" : QString();
            if (engine.isEmpty() || converter.isEmpty()) {
                QMessageBox::critical(win, "Can't create SVG",
                    "SVG export needs 'latex' and 'dvisvgm' on your system PATH. Both are included in "
                    "TeX Live and MiKTeX (in MiKTeX, install the 'dvisvgm' package if it is missing).\n\n"
                    "Alternatively install pdf2svg or Inkscape, or use \"Save .tex\" and convert elsewhere.");
                return;
            }
            route = "PDF -> " + converter;
            if (engine == "latexmk") steps.push_back({ "latexmk", { "-lualatex", "-interaction=nonstopmode", "-halt-on-error", "figure.tex" } });
            else steps.push_back({ engine, { "-interaction=nonstopmode", "-halt-on-error", "figure.tex" } });
            if (converter == "pdf2svg") steps.push_back({ "pdf2svg", { "figure.pdf", "figure.svg" } });
            else steps.push_back({ "inkscape", { "figure.pdf", "--export-type=svg", "--export-filename=figure.svg" } });
        }
        runBuild(win, status, { compileBtn, svgBtn }, svgCode, svgCsvFiles, steps, route, "figure.svg", "SVG");
    });

    auto *closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, win, &QDialog::close);
    row->addWidget(closeBtn);
    layout->addLayout(row);
    win->show();
}

void Latex3DExportDialog::runBuild(QDialog *win, QLabel *status, const QVector<QPushButton *> &buttons, const QString &tex,
                                   const QMap<QString, QString> &csvFiles, const QVector<ProcStep> &steps, const QString &route,
                                   const QString &outFile, const QString &what) {
    const QString tmp = QDir::temp().filePath(QString("uplotter3d_%1").arg(QDateTime::currentMSecsSinceEpoch()));
    if (!QDir().mkpath(tmp)) {
        QMessageBox::critical(win, what + " export failed", "Could not create a temporary directory.");
        return;
    }
    {
        QFile f(QDir(tmp).filePath("figure.tex"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(win, what + " export failed", "Could not write the temporary .tex file.");
            return;
        }
        QTextStream(&f) << tex;
    }
    for (auto it = csvFiles.constBegin(); it != csvFiles.constEnd(); ++it) {
        QFile cf(QDir(tmp).filePath(it.key()));
        if (cf.open(QIODevice::WriteOnly | QIODevice::Text)) QTextStream(&cf) << it.value();
    }

    for (QPushButton *b : buttons) b->setEnabled(false);
    status->setText(QString("Creating %1 (%2)...").arg(what, route));
    const QString outPath = QDir(tmp).filePath(outFile);
    const QString ext = QFileInfo(outFile).suffix();

    runProcessChain<ProcStep>(win, tmp, steps, [=](bool ok, bool timedOut, const QString &log) {
        for (QPushButton *b : buttons) b->setEnabled(true);
        status->setText("");
        if (ok && QFile::exists(outPath)) {
            const QString startDir = m_app->lastSaveDir().isEmpty() ? QDir::homePath() : m_app->lastSaveDir();
            QString dest = QFileDialog::getSaveFileName(win, "Save " + what, startDir + "/plot3d." + ext,
                                                        QString("%1 files (*.%2)").arg(what, ext));
            if (!dest.isEmpty()) {
                if (QFileInfo(dest).suffix().isEmpty()) dest += "." + ext;
                m_app->setLastSaveDir(QFileInfo(dest).absolutePath());
                QFile::remove(dest);
                if (QFile::copy(outPath, dest)) {
                    auto reply = QMessageBox::question(win, what + " saved", what + " saved to:\n" + dest + "\n\nOpen it now?",
                                                       QMessageBox::Yes | QMessageBox::No);
                    if (reply == QMessageBox::Yes) QDesktopServices::openUrl(QUrl::fromLocalFile(dest));
                } else {
                    QMessageBox::critical(win, "Save failed", "Could not copy the " + what + " to that location.");
                }
            }
        } else if (timedOut) {
            QMessageBox::critical(win, what + " export failed", "Timed out after 120 seconds.");
        } else {
            showLogWindow(win, log);
        }
        QDir(tmp).removeRecursively();
    });
}
