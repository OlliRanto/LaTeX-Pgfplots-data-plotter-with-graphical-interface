#include "CurveFit.h"
#include "ExprEval.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>
#include <QSet>

namespace {

// ---- tiny dense linear algebra: solve A x = b via Gaussian elimination
// with partial pivoting. A is n x n (row-major vector<vector<double>>). ----
QVector<double> solveLinear(QVector<QVector<double>> A, QVector<double> b) {
    int n = b.size();
    for (int col = 0; col < n; ++col) {
        int piv = col;
        double best = std::fabs(A[col][col]);
        for (int r = col + 1; r < n; ++r) {
            if (std::fabs(A[r][col]) > best) { best = std::fabs(A[r][col]); piv = r; }
        }
        if (best < 1e-300) throw std::runtime_error("singular matrix in fit");
        std::swap(A[col], A[piv]);
        std::swap(b[col], b[piv]);
        for (int r = col + 1; r < n; ++r) {
            double f = A[r][col] / A[col][col];
            for (int c = col; c < n; ++c) A[r][c] -= f * A[col][c];
            b[r] -= f * b[col];
        }
    }
    QVector<double> x(n, 0.0);
    for (int i = n - 1; i >= 0; --i) {
        double s = b[i];
        for (int j = i + 1; j < n; ++j) s -= A[i][j] * x[j];
        x[i] = s / A[i][i];
    }
    return x;
}

// Least-squares polynomial fit (normal equations), degree-th order,
// coefficients returned highest-degree-first (numpy.polyfit convention).
QVector<double> polyfit(const QVector<double> &xs, const QVector<double> &ys, int degree) {
    int m = degree + 1;
    QVector<QVector<double>> AtA(m, QVector<double>(m, 0.0));
    QVector<double> Atb(m, 0.0);
    for (int i = 0; i < xs.size(); ++i) {
        QVector<double> powers(m);
        double p = 1.0;
        for (int k = m - 1; k >= 0; --k) { powers[k] = p; p *= xs[i]; }
        for (int r = 0; r < m; ++r) {
            Atb[r] += powers[r] * ys[i];
            for (int c = 0; c < m; ++c) AtA[r][c] += powers[r] * powers[c];
        }
    }
    return solveLinear(AtA, Atb); // index 0 == highest degree coeff
}

double polyval(const QVector<double> &coeffs, double x) {
    double r = 0.0;
    for (double c : coeffs) r = r * x + c;
    return r;
}

// Levenberg-Marquardt with numeric (central-difference) Jacobian.
// model(x, params) -> y. Returns fitted params, throws on non-convergence.
QVector<double> levenbergMarquardt(
    const std::function<double(double, const QVector<double> &)> &model,
    const QVector<double> &xs, const QVector<double> &ys,
    QVector<double> params, int maxIter = 500) {

    int n = params.size();
    int m = xs.size();
    auto cost = [&](const QVector<double> &p) {
        double s = 0;
        for (int i = 0; i < m; ++i) { double r = ys[i] - model(xs[i], p); s += r * r; }
        return s;
    };

    double lambda = 1e-3;
    double curCost = cost(params);
    if (!std::isfinite(curCost)) throw std::runtime_error("did not converge (invalid initial guess)");

    for (int iter = 0; iter < maxIter; ++iter) {
        // Jacobian: dResidual/dParam_j at each sample. residual_i = y_i - model(x_i,p)
        QVector<QVector<double>> J(m, QVector<double>(n));
        QVector<double> resid(m);
        for (int i = 0; i < m; ++i) resid[i] = ys[i] - model(xs[i], params);

        for (int j = 0; j < n; ++j) {
            double h = std::max(1e-6, std::fabs(params[j]) * 1e-6);
            QVector<double> pPlus = params, pMinus = params;
            pPlus[j] += h; pMinus[j] -= h;
            for (int i = 0; i < m; ++i) {
                double yPlus = model(xs[i], pPlus);
                double yMinus = model(xs[i], pMinus);
                J[i][j] = -(yPlus - yMinus) / (2 * h); // d(residual)/dp
            }
        }

        QVector<QVector<double>> JtJ(n, QVector<double>(n, 0.0));
        QVector<double> Jtr(n, 0.0);
        for (int i = 0; i < m; ++i) {
            for (int a = 0; a < n; ++a) {
                Jtr[a] += J[i][a] * resid[i];
                for (int b = 0; b < n; ++b) JtJ[a][b] += J[i][a] * J[i][b];
            }
        }

        bool improved = false;
        for (int attempt = 0; attempt < 20 && !improved; ++attempt) {
            QVector<QVector<double>> A = JtJ;
            for (int d = 0; d < n; ++d) A[d][d] *= (1.0 + lambda);
            QVector<double> negJtr(n);
            for (int i = 0; i < n; ++i) negJtr[i] = -Jtr[i];
            QVector<double> delta;
            try {
                delta = solveLinear(A, negJtr);
            } catch (...) {
                lambda *= 10;
                continue;
            }
            QVector<double> trial(n);
            for (int i = 0; i < n; ++i) trial[i] = params[i] + delta[i];
            double trialCost = cost(trial);
            if (std::isfinite(trialCost) && trialCost < curCost) {
                params = trial;
                if (std::fabs(curCost - trialCost) < 1e-14 * (curCost + 1e-14)) {
                    curCost = trialCost;
                    improved = true;
                    iter = maxIter; // converged, break outer loop too
                    break;
                }
                curCost = trialCost;
                lambda = std::max(lambda / 10, 1e-12);
                improved = true;
            } else {
                lambda *= 10;
            }
        }
        if (!improved) {
            // couldn't improve this iteration; treat as converged if cost is finite
            if (std::isfinite(curCost)) break;
            throw std::runtime_error("did not converge");
        }
    }
    if (!std::isfinite(curCost)) throw std::runtime_error("did not converge");
    return params;
}

QString fmtG(double v, int sig = 4) {
    return QString::number(v, 'g', sig);
}

} // namespace

FitResult performFit(const QVector<double> &xs, const QVector<double> &ys,
                      double xmin, double xmax, const FitOptions &opts) {
    if (xs.size() < 2) throw std::runtime_error("need at least 2 points to fit");

    FitResult out;
    out.plotXmin = xmin;
    out.plotXmax = xmax;

    if (opts.kind == FitKind::Linear) {
        QVector<double> c = polyfit(xs, ys, 1); // [m, b]
        double m = c[0], b = c[1];
        out.func = [m, b](double x) { return m * x + b; };
        QString sign = b >= 0 ? "+" : "-";
        out.formula = QString("y = %1x %2 %3").arg(fmtG(m, 5), sign, fmtG(std::fabs(b), 5));
        out.kind = FitKind::Linear;
        out.params["m"] = m; out.params["b"] = b;
        out.xsUsed = xs; out.ysUsed = ys;
        return out;
    }

    if (opts.kind == FitKind::Polynomial) {
        int deg = std::max(1, opts.polyDegree);
        if (xs.size() <= deg)
            throw std::runtime_error(QString("need more than %1 points for degree %1").arg(deg).toStdString());
        QVector<double> coeffs = polyfit(xs, ys, deg);
        out.func = [coeffs](double x) { return polyval(coeffs, x); };
        int n = coeffs.size();
        QStringList terms;
        for (int i = 0; i < n; ++i) {
            int p = n - 1 - i;
            double c = coeffs[i];
            if (p == 0) terms << fmtG(c);
            else if (p == 1) terms << (fmtG(c) + "x");
            else terms << QString("%1x^%2").arg(fmtG(c)).arg(p);
        }
        QString formula = "y = " + terms.join(" + ");
        formula.replace("+ -", "- ");
        out.formula = formula;
        out.kind = FitKind::Polynomial;
        out.polyCoeffs = coeffs;
        out.xsUsed = xs; out.ysUsed = ys;
        return out;
    }

    if (opts.kind == FitKind::Exponential) {
        double yMin = *std::min_element(ys.begin(), ys.end());
        double yMax = *std::max_element(ys.begin(), ys.end());
        double span = (yMax - yMin) != 0 ? (yMax - yMin) : 1.0;
        int argMaxX = 0, argMinX = 0;
        for (int i = 1; i < xs.size(); ++i) { if (xs[i] > xs[argMaxX]) argMaxX = i; if (xs[i] < xs[argMinX]) argMinX = i; }
        bool rising = ys[argMaxX] >= ys[argMinX];
        double a0 = rising ? span : -span;
        double c0 = yMin;

        auto model = [](double x, const QVector<double> &p) {
            double bx = p[1] * x;
            bx = std::clamp(bx, -700.0, 700.0);
            return p[0] * std::exp(bx) + p[2];
        };
        QVector<double> p0 = { a0, 0.01, c0 };
        QVector<double> popt;
        try {
            popt = levenbergMarquardt(model, xs, ys, p0);
        } catch (std::exception &e) {
            throw std::runtime_error(QString("did not converge (%1)").arg(e.what()).toStdString());
        }
        double a = popt[0], b = popt[1], c = popt[2];
        out.func = [a, b, c](double x) { return a * std::exp(std::clamp(b * x, -700.0, 700.0)) + c; };
        QString sign = c >= 0 ? "+" : "-";
        out.formula = QString::fromUtf8("y = %1\u00b7e^(%2x) %3 %4")
                          .arg(fmtG(a, 5)).arg(fmtG(b, 5)).arg(sign).arg(fmtG(std::fabs(c), 5));
        out.kind = FitKind::Exponential;
        out.params["a"] = a; out.params["b"] = b; out.params["c"] = c;
        out.xsUsed = xs; out.ysUsed = ys;
        return out;
    }

    if (opts.kind == FitKind::Logarithmic) {
        QVector<double> xsL, ysL;
        for (int i = 0; i < xs.size(); ++i) if (xs[i] > 0) { xsL << xs[i]; ysL << ys[i]; }
        if (xsL.size() < 2) throw std::runtime_error("needs at least 2 points with x > 0");
        QVector<double> logXs;
        for (double x : xsL) logXs << std::log(x);
        QVector<double> c = polyfit(logXs, ysL, 1);
        double a = c[0], b = c[1];
        out.func = [a, b](double x) { return a * std::log(x) + b; };
        QString sign = b >= 0 ? "+" : "-";
        out.formula = QString::fromUtf8("y = %1\u00b7ln(x) %2 %3").arg(fmtG(a, 5)).arg(sign).arg(fmtG(std::fabs(b), 5));
        out.kind = FitKind::Logarithmic;
        out.params["a"] = a; out.params["b"] = b;
        out.xsUsed = xsL; out.ysUsed = ysL;
        double xMinL = *std::min_element(xsL.begin(), xsL.end());
        out.plotXmin = std::max(xmin, xMinL);
        return out;
    }

    if (opts.kind == FitKind::Power) {
        QVector<double> xsL, ysL;
        for (int i = 0; i < xs.size(); ++i) if (xs[i] > 0) { xsL << xs[i]; ysL << ys[i]; }
        if (xsL.size() < 2) throw std::runtime_error("needs at least 2 points with x > 0");
        auto model = [](double x, const QVector<double> &p) { return p[0] * std::pow(x, p[1]); };
        double a0 = ysL[0] != 0 ? ysL[0] : 1.0;
        QVector<double> p0 = { a0, 1.0 };
        QVector<double> popt;
        try {
            popt = levenbergMarquardt(model, xsL, ysL, p0);
        } catch (std::exception &e) {
            throw std::runtime_error(QString("did not converge (%1)").arg(e.what()).toStdString());
        }
        double a = popt[0], b = popt[1];
        out.func = [a, b](double x) { return a * std::pow(x, b); };
        out.formula = QString::fromUtf8("y = %1\u00b7x^%2").arg(fmtG(a, 5)).arg(fmtG(b, 5));
        out.kind = FitKind::Power;
        out.params["a"] = a; out.params["b"] = b;
        out.xsUsed = xsL; out.ysUsed = ysL;
        double xMinL = *std::min_element(xsL.begin(), xsL.end());
        out.plotXmin = std::max(xmin, xMinL);
        return out;
    }

    if (opts.kind == FitKind::Custom) {
        if (opts.customExpr.trimmed().isEmpty()) throw std::runtime_error("enter a function of x");
        if (opts.customParamNames.isEmpty()) throw std::runtime_error("enter at least one parameter name");
        if (opts.customGuesses.size() != opts.customParamNames.size())
            throw std::runtime_error("number of guesses must match number of parameters");

        // Parameter names must not collide with the data variable, built-in
        // constants/functions, or each other (ExprEval would silently ignore them).
        static const QSet<QString> reserved = {
            "x", "pi", "e", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh",
            "tanh", "exp", "log", "log10", "log2", "sqrt", "abs", "power"
        };
        QSet<QString> seen;
        for (const QString &nm : opts.customParamNames) {
            if (nm.isEmpty() || !(nm[0].isLetter() || nm[0] == '_'))
                throw std::runtime_error(QString("invalid parameter name '%1'").arg(nm).toStdString());
            for (QChar ch : nm)
                if (!(ch.isLetterOrNumber() || ch == '_'))
                    throw std::runtime_error(QString("invalid parameter name '%1'").arg(nm).toStdString());
            if (reserved.contains(nm))
                throw std::runtime_error(QString("parameter name '%1' is reserved (x, pi, e and function names cannot be used)").arg(nm).toStdString());
            if (seen.contains(nm))
                throw std::runtime_error(QString("duplicate parameter name '%1'").arg(nm).toStdString());
            seen.insert(nm);
        }

        // NOTE: everything is captured BY VALUE. The returned FitResult::func outlives
        // this function, so capturing locals by reference would leave it dangling.
        ExprEval expr(opts.customExpr);
        QStringList paramNames = opts.customParamNames;
        auto model = [expr, paramNames](double x, const QVector<double> &p) {
            QMap<QString, double> vars;
            vars["x"] = x;
            for (int i = 0; i < paramNames.size(); ++i) vars[paramNames[i]] = p[i];
            return expr.evaluate(vars);
        };

        // Validate the expression once up front so syntax errors give a clear message.
        try {
            model(xs[0], opts.customGuesses);
        } catch (std::exception &e) {
            throw std::runtime_error(QString("invalid expression (%1)").arg(e.what()).toStdString());
        }

        QVector<double> popt;
        try {
            popt = levenbergMarquardt(model, xs, ys, opts.customGuesses);
        } catch (std::exception &e) {
            throw std::runtime_error(QString("did not converge (%1)").arg(e.what()).toStdString());
        }

        // Callers invoke func outside any try/catch, so it must never throw.
        out.func = [model, popt](double x) -> double {
            try { return model(x, popt); }
            catch (...) { return std::numeric_limits<double>::quiet_NaN(); }
        };
        QStringList paramStrs;
        for (int i = 0; i < paramNames.size(); ++i)
            paramStrs << QString("%1=%2").arg(paramNames[i], fmtG(popt[i], 5));
        out.formula = QString("y = %1\n  (%2)").arg(opts.customExpr, paramStrs.join(", "));
        out.kind = FitKind::Custom;
        out.customExpr = opts.customExpr;
        out.customParamNames = paramNames;
        out.customParamValues = popt;
        out.xsUsed = xs; out.ysUsed = ys;
        return out;
    }

    throw std::runtime_error("unknown fit type");
}
