#include "ColumnStatDialog.h"

#include <QListWidget>
#include <QSpinBox>
#include <QLabel>
#include <QRadioButton>
#include <QLineEdit>
#include <QDoubleValidator>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QBrush>
#include <cmath>
#include <limits>

static double statOf(const QVector<double> &v, const QVector<int> &idx, ColumnStatDialog::Kind kind, bool sample, int *used) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    long double sum = 0;
    int n = 0;
    for (int i : idx)
        if (std::isfinite(v[i])) { sum += v[i]; ++n; }
    if (used) *used = n;
    if (n == 0) return nan;
    const long double mean = sum / n;
    if (kind == ColumnStatDialog::Kind::Mean) return double(mean);
    if (sample && n < 2) return nan;
    long double ss = 0;
    for (int i : idx)
        if (std::isfinite(v[i])) ss += (v[i] - mean) * (v[i] - mean);
    return double(std::sqrt(ss / (sample ? n - 1 : n)));
}

double ColumnStatDialog::compute(const QVector<double> &v, int from, int to, Kind kind, bool sample, int *used) {
    from = std::max(1, from);
    to = std::min(int(v.size()), to);
    QVector<int> idx;
    for (int i = from - 1; i < to; ++i) idx.append(i);
    return statOf(v, idx, kind, sample, used);
}

double ColumnStatDialog::computeByX(const QVector<double> &v, const QVector<double> &x, double xlo, double xhi, Kind kind,
                                    bool sample, int *used, int *total) {
    if (xlo > xhi) std::swap(xlo, xhi);
    QVector<int> idx;
    for (int i = 0; i < v.size() && i < x.size(); ++i)
        if (std::isfinite(x[i]) && x[i] >= xlo && x[i] <= xhi) idx.append(i);
    if (total) *total = int(idx.size());
    return statOf(v, idx, kind, sample, used);
}

QString ColumnStatDialog::resultText() const { return QString::number(m_result, 'g', 12); }

ColumnStatDialog::ColumnStatDialog(Kind kind, const QString &targetCell, const QVector<StatColumn> &columns, QWidget *parent)
    : QDialog(parent), m_kind(kind), m_cols(columns) {
    const bool mean = kind == Kind::Mean;
    setWindowTitle(mean ? "Mean of a column" : "Standard deviation of a column");
    resize(460, 520);
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(QString("Select the column to calculate the %1 of (the result is written into cell %2):")
                                 .arg(mean ? "mean" : "standard deviation", targetCell));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // same kind of list as the Y selector of the main window: "file: column", in the colour of the file
    list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setMinimumHeight(140);
    for (const StatColumn &c : m_cols) {
        auto *item = new QListWidgetItem(c.label);
        if (c.color.isValid()) item->setForeground(QBrush(c.color));
        list->addItem(item);
    }
    layout->addWidget(list, 1);

    auto *rangeBox = new QGroupBox("Range of points");
    auto *rl = new QVBoxLayout(rangeBox);
    auto *modeRow = new QHBoxLayout;
    byPointRadio = new QRadioButton("By point number");
    byXRadio = new QRadioButton("By X value");
    byXRadio->setToolTip("Use all points whose X value lies between the two limits (inclusive)");
    byPointRadio->setChecked(true);
    modeRow->addWidget(byPointRadio);
    modeRow->addWidget(byXRadio);
    modeRow->addStretch();
    rl->addLayout(modeRow);
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel("From point:"));
    fromSpin = new QSpinBox;
    fromSpin->setToolTip("First point (row of the column, counting from 1) that is used");
    row->addWidget(fromSpin);
    row->addWidget(new QLabel("to point:"));
    toSpin = new QSpinBox;
    toSpin->setToolTip("Last point (row of the column, counting from 1) that is used");
    row->addWidget(toSpin);
    m_rangeInfo = new QLabel;
    m_rangeInfo->setStyleSheet("color:#555;");
    row->addWidget(m_rangeInfo);
    row->addStretch();
    auto *whole = new QPushButton("Whole column");
    whole->setToolTip("Use all points of the column");
    row->addWidget(whole);
    rl->addLayout(row);
    auto *xrow = new QHBoxLayout;
    xrow->addWidget(new QLabel("From X:"));
    xFromEdit = new QLineEdit;
    xFromEdit->setValidator(new QDoubleValidator(xFromEdit));
    xrow->addWidget(xFromEdit);
    xrow->addWidget(new QLabel("to X:"));
    xToEdit = new QLineEdit;
    xToEdit->setValidator(new QDoubleValidator(xToEdit));
    xrow->addWidget(xToEdit);
    rl->addLayout(xrow);
    m_xInfo = new QLabel;
    m_xInfo->setStyleSheet("color:#555;");
    rl->addWidget(m_xInfo);
    layout->addWidget(rangeBox);

    if (!mean) {
        auto *kb = new QGroupBox("Standard deviation of");
        auto *kl = new QHBoxLayout(kb);
        sampleRadio = new QRadioButton("a sample (divide by n - 1)");
        sampleRadio->setToolTip("The usual choice for measured data: the estimate of the spread of the whole population");
        populationRadio = new QRadioButton("the whole population (divide by n)");
        sampleRadio->setChecked(true);
        kl->addWidget(sampleRadio);
        kl->addWidget(populationRadio);
        layout->addWidget(kb);
        connect(sampleRadio, &QRadioButton::toggled, this, [this]() { update(); });
    }

    resultLabel = new QLabel;
    QFont f = resultLabel->font();
    f.setBold(true);
    resultLabel->setFont(f);
    layout->addWidget(resultLabel);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    okButton = new QPushButton(QString("Insert into cell %1").arg(targetCell));
    okButton->setDefault(true);
    auto *cancel = new QPushButton("Cancel");
    buttons->addWidget(okButton);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(list, &QListWidget::currentRowChanged, this, [this]() { columnChanged(); });
    connect(whole, &QPushButton::clicked, this, [this]() {
        fromSpin->setValue(fromSpin->minimum());
        toSpin->setValue(toSpin->maximum());
        columnChanged();   // also resets the X limits to the full X extent
    });
    connect(byXRadio, &QRadioButton::toggled, this, [this]() { modeChanged(); });
    connect(xFromEdit, &QLineEdit::textChanged, this, [this]() { update(); });
    connect(xToEdit, &QLineEdit::textChanged, this, [this]() { update(); });
    // the range stays valid: from <= to
    connect(fromSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { if (toSpin->value() < v) toSpin->setValue(v); update(); });
    connect(toSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { if (fromSpin->value() > v) fromSpin->setValue(v); update(); });

    if (!m_cols.isEmpty()) list->setCurrentRow(0);
    else columnChanged();
}

void ColumnStatDialog::columnChanged() {
    const int r = list->currentRow();
    const int n = (r >= 0 && r < m_cols.size()) ? int(m_cols[r].values.size()) : 0;
    fromSpin->blockSignals(true);
    toSpin->blockSignals(true);
    fromSpin->setRange(n > 0 ? 1 : 0, n);
    toSpin->setRange(n > 0 ? 1 : 0, n);
    fromSpin->setValue(n > 0 ? 1 : 0);
    toSpin->setValue(n);
    fromSpin->blockSignals(false);
    toSpin->blockSignals(false);
    fromSpin->setEnabled(n > 0);
    toSpin->setEnabled(n > 0);
    m_rangeInfo->setText(n > 0 ? QString("(of %1 points)").arg(n) : QString());
    // X limits: the full X extent of the column; X mode is only possible when the column has an X
    double lo = 0, hi = 0;
    bool have = false;
    if (n > 0 && r >= 0)
        for (double xv : m_cols[r].x)
            if (std::isfinite(xv)) { if (!have) lo = hi = xv; else { lo = std::min(lo, xv); hi = std::max(hi, xv); } have = true; }
    xFromEdit->blockSignals(true);
    xToEdit->blockSignals(true);
    xFromEdit->setText(have ? QString::number(lo, 'g', 10) : QString());
    xToEdit->setText(have ? QString::number(hi, 'g', 10) : QString());
    xFromEdit->blockSignals(false);
    xToEdit->blockSignals(false);
    byXRadio->setEnabled(have);
    if (!have && byXRadio->isChecked()) byPointRadio->setChecked(true);
    modeChanged();
}

void ColumnStatDialog::modeChanged() {
    const bool byX = byXRadio->isChecked();
    fromSpin->setEnabled(!byX && fromSpin->maximum() > 0);
    toSpin->setEnabled(!byX && toSpin->maximum() > 0);
    xFromEdit->setEnabled(byX);
    xToEdit->setEnabled(byX);
    update();
}

void ColumnStatDialog::update() {
    const bool mean = m_kind == Kind::Mean;
    const int r = list->currentRow();
    m_valid = false;
    if (r < 0 || r >= m_cols.size()) {
        resultLabel->setText(m_cols.isEmpty() ? "There is no numeric data to use. Load a file or type numbers into the table first."
                                              : "Select a column.");
        m_xInfo->clear();
        okButton->setEnabled(false);
        return;
    }
    const StatColumn &c = m_cols[r];
    const int from = fromSpin->value(), to = toSpin->value();
    int total = to - from + 1;
    const bool byX = byXRadio->isChecked();
    bool xOk = true;
    double xlo = xFromEdit->text().toDouble(&xOk), xhi = 0;
    bool xOk2 = true;
    xhi = xToEdit->text().toDouble(&xOk2);
    if (byX && (!xOk || !xOk2)) {
        resultLabel->setText("Enter both X limits as numbers.");
        m_xInfo->clear();
        okButton->setEnabled(false);
        return;
    }
    if (byX) m_xInfo->setText(QString("%1 from %2 to %3").arg(c.xName.isEmpty() ? "X" : c.xName).arg(std::min(xlo, xhi), 0, 'g', 6).arg(std::max(xlo, xhi), 0, 'g', 6));
    else if (!c.x.isEmpty() && from >= 1 && to <= c.x.size())
        m_xInfo->setText(QString("%1 from %2 to %3").arg(c.xName.isEmpty() ? "X" : c.xName)
                             .arg(c.x[from - 1], 0, 'g', 6).arg(c.x[to - 1], 0, 'g', 6));
    else
        m_xInfo->clear();
    int used = 0;
    const bool sampleSd = !sampleRadio || sampleRadio->isChecked();
    const double v = byX ? computeByX(c.values, c.x, xlo, xhi, m_kind, sampleSd, &used, &total)
                         : compute(c.values, from, to, m_kind, sampleSd, &used);
    QString name = mean ? "Mean" : (sampleRadio && !sampleRadio->isChecked() ? "Standard deviation (population)" : "Standard deviation (sample)");
    if (std::isnan(v)) {
        resultLabel->setText(used == 0 ? (byX ? "No numeric values with X in this range." : "No numeric values in this range.")
                                       : QString("%1 needs at least 2 values in the range (found %2).").arg(name).arg(used));
        okButton->setEnabled(false);
        return;
    }
    m_result = v;
    m_valid = true;
    QString text = QString("%1 = %2   (%3 values").arg(name, QString::number(v, 'g', 10)).arg(used);
    if (used < total) text += QString(", %1 empty / non-numeric skipped").arg(total - used);
    resultLabel->setText(text + ")");
    okButton->setEnabled(true);
}
