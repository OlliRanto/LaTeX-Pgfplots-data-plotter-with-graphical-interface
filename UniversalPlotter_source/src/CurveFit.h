#pragma once
#include <QString>
#include <QVector>
#include <QMap>
#include <functional>
#include <stdexcept>

enum class FitKind { Linear, Polynomial, Exponential, Logarithmic, Power, Custom };

inline QStringList fitTypeNames() {
    return { "Linear", "Polynomial", QString::fromUtf8("Exponential  (a\u00b7e^(bx) + c)"),
             QString::fromUtf8("Logarithmic  (a\u00b7ln(x) + b)"), QString::fromUtf8("Power  (a\u00b7x^b)"),
             "Custom" };
}

struct FitResult {
    std::function<double(double)> func;
    QString formula;
    QVector<double> xsUsed, ysUsed;
    double plotXmin = 0, plotXmax = 0;
    FitKind kind = FitKind::Linear;
    QMap<QString, double> params; // scalar params (a,b,c,m, etc.)
    QVector<double> polyCoeffs;   // for Polynomial (highest degree first)
    QString customExpr;           // for Custom
    QStringList customParamNames; // for Custom
    QVector<double> customParamValues;
    double r2 = 0.0;
    int n = 0;
};

struct FitOptions {
    FitKind kind = FitKind::Linear;
    int polyDegree = 2;
    QString customExpr;
    QStringList customParamNames;
    QVector<double> customGuesses;
};

// Runs the requested fit on (xs, ys). xmin/xmax are the (already-filtered)
// domain used to draw the curve. Throws std::runtime_error on failure,
// mirroring the Python app's ValueError-raising _perform_fit().
FitResult performFit(const QVector<double> &xs, const QVector<double> &ys,
                      double xmin, double xmax, const FitOptions &opts);
