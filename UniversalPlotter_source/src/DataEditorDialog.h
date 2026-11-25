#pragma once
#include <QDialog>
#include "Dataset.h"
#include "NoiseFilter.h"

class QComboBox;
class QListWidget;
class QLabel;
class QSpinBox;
class QLineEdit;
class QDoubleSpinBox;
class QCheckBox;
class QTabBar;
class QWidget;
class MainWindow;

// "Data editor" window: pick a dataset, its X column and one or more Y columns, then create new data from them
// (n-th derivative, n-th integral, absolute value, sign flipped). The results are added to the plotter's
// data selector as a dataset called "<name> (edited)" whose columns end in (derivative), (2. derivative),
// (integral), (2. integral), (abs), (sign flipped) or (<math expression>). A plotted function is edited as math:
// the result is a new formula, e.g. sign flip of x^2 + 3*x gives -x^2 - 3*x, "*2" on sin(x) gives sin(x)*2 and
// the derivative of sin(x) is cos(x) and its integral is -cos(x) + 1 (0 at the start of the domain).
// (A result without a known formula, e.g. the integral of x*sin(x), is made as sampled points.)
// The "Noise removal" tab smooths / cleans the selected columns with one of 17 methods (moving average, Gaussian,
// exponential, Holt, Savitzky-Golay, median, Hampel, outlier removal, LOWESS, Whittaker, total variation, bilateral,
// Wiener, Kalman, Butterworth, FFT, wavelet); each method shows its own parameters. The result is a column called
// "<name> (<method and parameters>)", e.g. "y (Savitzky-Golay, window 11, order 3)". The maths is in NoiseFilter.cpp. The edited columns are listed in this window and can be deleted here.
class DataEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit DataEditorDialog(MainWindow *app);
    void refresh();   // re-reads the datasets of the main window (called when the window is shown / activated)

protected:
    void showEvent(QShowEvent *e) override;
    void changeEvent(QEvent *e) override;

private slots:
    void onDatasetChanged();
    void onXChanged();
    void deleteSelected();
    void deleteAll();
    void updateSamplingInfo();       // point count / spacing / Nyquist frequency of the selected data (noise removal tab)

private:
    enum class Op { Derivative, Integral, Abs, Negate, Expr, Denoise };
    void apply(Op op);
    void fillYList();
    void fillYListItems();
    void fillEditedList();
    QWidget *buildNoiseTab();
    nf::Params readNoiseParams() const;

    MainWindow *m_app;
    QComboBox *m_dsCombo = nullptr;
    QComboBox *m_xCombo = nullptr;
    QListWidget *m_yList = nullptr;
    QListWidget *m_editedList = nullptr;
    QLineEdit *m_exprEdit = nullptr;     // math expression, e.g. "*2" or "y^2 + 1"
    QSpinBox *m_orderSpin = nullptr;     // 1 = first derivative / integral, 2 = second, ...
    QLabel *m_status = nullptr;
    bool m_refreshing = false;
    double m_nyquist = 0;                // Nyquist frequency of the selected data (0 = unknown)

    // Widgets of the "Noise removal" tab. The pages of the stack are in the order of nf::Method.
    struct NoiseUi {
        QComboBox *method = nullptr;
        QWidget *pages = nullptr;              // host of the parameter pages; only the page of the chosen method is shown
        QVector<QWidget *> pageList;           // the pages, in the order of nf::Method
        QLabel *info = nullptr;
        // window based
        QSpinBox *maWindow = nullptr, *medWindow = nullptr, *hampWindow = nullptr, *sgWindow = nullptr, *bilWindow = nullptr, *wieWindow = nullptr;
        QComboBox *maWeights = nullptr, *maEdge = nullptr, *gaEdge = nullptr, *medEdge = nullptr, *hampEdge = nullptr, *bilEdge = nullptr, *wieEdge = nullptr;
        QDoubleSpinBox *gaSigma = nullptr;
        // exponential / Holt
        QDoubleSpinBox *emaAlpha = nullptr, *holtAlpha = nullptr, *holtBeta = nullptr;
        QComboBox *emaDir = nullptr;
        // Savitzky-Golay
        QSpinBox *sgOrder = nullptr;
        // Hampel / outliers
        QDoubleSpinBox *hampSigmas = nullptr, *outThreshold = nullptr;
        QComboBox *outRule = nullptr, *outReplace = nullptr;
        // LOWESS
        QDoubleSpinBox *lowFraction = nullptr;
        QComboBox *lowDegree = nullptr;
        QSpinBox *lowRobust = nullptr;
        // Whittaker / total variation
        QDoubleSpinBox *whLambda = nullptr, *tvLambda = nullptr;
        QSpinBox *whOrder = nullptr, *tvIters = nullptr;
        // bilateral / Wiener / Kalman
        QDoubleSpinBox *bilSigmaS = nullptr, *bilSigmaR = nullptr, *wieNoise = nullptr, *kalQ = nullptr;
        QCheckBox *kalSmooth = nullptr;
        // Butterworth
        QComboBox *bwType = nullptr, *bwUnit = nullptr;
        QSpinBox *bwOrder = nullptr;
        QDoubleSpinBox *bwC1 = nullptr, *bwC2 = nullptr;
        QCheckBox *bwZero = nullptr;
        // FFT
        QComboBox *fftMode = nullptr, *fftUnit = nullptr;
        QDoubleSpinBox *fftC1 = nullptr, *fftC2 = nullptr, *fftKeep = nullptr;
        // wavelet
        QComboBox *wavFamily = nullptr, *wavRule = nullptr, *wavType = nullptr;
        QSpinBox *wavLevels = nullptr;
        QDoubleSpinBox *wavThreshold = nullptr;
    } m_nu;
};
