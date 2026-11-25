#include "NoiseFilter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <sstream>

namespace nf {
namespace {

using Vec = std::vector<double>;
using cd = std::complex<double>;
const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------------------------------------ small helpers

int oddWindow(int w, int m) {
    w = std::max(w, 1);
    if (w % 2 == 0) ++w;
    const int maxOdd = (m % 2) ? m : m - 1;
    return std::max(1, std::min(w, maxOdd));
}

// Index j of a window position, folded back into 0..m-1 according to the edge rule (-1 = leave the sample out).
int mapIndex(int j, int m, Edge e) {
    if (j >= 0 && j < m) return j;
    if (e == Edge::Shrink) return -1;
    if (e == Edge::Nearest) return j < 0 ? 0 : m - 1;
    if (m == 1) return 0;
    const int period = 2 * (m - 1);                      // mirror without repeating the edge sample
    j %= period;
    if (j < 0) j += period;
    return j < m ? j : period - j;
}

double medianOf(Vec &v) {                               // (reorders v)
    if (v.empty()) return kNaN;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    const double hi = v[mid];
    if (v.size() % 2) return hi;
    const double lo = *std::max_element(v.begin(), v.begin() + mid);
    return 0.5 * (lo + hi);
}

double percentileOf(Vec v, double q) {                  // linear interpolation, q in 0..1
    std::sort(v.begin(), v.end());
    const double pos = q * (v.size() - 1);
    const size_t a = size_t(std::floor(pos)), b = std::min(a + 1, v.size() - 1);
    return v[a] + (pos - a) * (v[b] - v[a]);
}

// Noise level of y from the differences of neighbouring points (robust against the signal itself).
double noiseSigma(const Vec &y) {
    if (y.size() < 3) return 0;
    Vec d;
    for (size_t i = 0; i + 1 < y.size(); ++i) d.push_back(std::fabs(y[i + 1] - y[i]));
    return 1.4826 * medianOf(d) / std::sqrt(2.0);
}

// Solves the small dense system A c = b (Gaussian elimination, partial pivoting). false when singular.
bool solveDense(std::vector<Vec> &A, Vec &b) {
    const int n = int(b.size());
    for (int c = 0; c < n; ++c) {
        int piv = c;
        for (int r = c + 1; r < n; ++r) if (std::fabs(A[r][c]) > std::fabs(A[piv][c])) piv = r;
        if (std::fabs(A[piv][c]) < 1e-12) return false;
        std::swap(A[piv], A[c]);
        std::swap(b[piv], b[c]);
        for (int r = c + 1; r < n; ++r) {
            const double f = A[r][c] / A[c][c];
            for (int k = c; k < n; ++k) A[r][k] -= f * A[c][k];
            b[r] -= f * b[c];
        }
    }
    for (int r = n - 1; r >= 0; --r) {
        for (int k = r + 1; k < n; ++k) b[r] -= A[r][k] * b[k];
        b[r] /= A[r][r];
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ window filters

Vec windowWeights(Window w, int h) {                     // 2h+1 weights, k = -h..h
    Vec out(2 * h + 1);
    for (int k = -h; k <= h; ++k) {
        const double t = kPi * k / (h + 1);
        double v = 1;
        switch (w) {
        case Window::Uniform: v = 1; break;
        case Window::Triangular: v = 1.0 - std::fabs(double(k)) / (h + 1); break;
        case Window::Hann: v = 0.5 * (1 + std::cos(t)); break;
        case Window::Hamming: v = 0.54 + 0.46 * std::cos(t); break;
        case Window::Blackman: v = 0.42 + 0.5 * std::cos(t) + 0.08 * std::cos(2 * t); break;
        }
        out[k + h] = v;
    }
    return out;
}

Vec convolve(const Vec &y, const Vec &weights, Edge edge) {
    const int m = int(y.size()), h = int(weights.size()) / 2;
    Vec out(m);
    for (int i = 0; i < m; ++i) {
        double s = 0, ws = 0;
        for (int k = -h; k <= h; ++k) {
            const int j = mapIndex(i + k, m, edge);
            if (j < 0) continue;
            s += weights[k + h] * y[j];
            ws += weights[k + h];
        }
        out[i] = ws > 0 ? s / ws : y[i];
    }
    return out;
}

Vec emaForward(const Vec &v, double a) {
    Vec o(v.size());
    if (v.empty()) return o;
    o[0] = v[0];
    for (size_t i = 1; i < v.size(); ++i) o[i] = a * v[i] + (1 - a) * o[i - 1];
    return o;
}

Vec exponential(const Vec &y, double a, Direction dir) {
    if (dir == Direction::Forward) return emaForward(y, a);
    Vec r(y.rbegin(), y.rend());
    if (dir == Direction::Backward) {
        r = emaForward(r, a);
        return Vec(r.rbegin(), r.rend());
    }
    Vec f = emaForward(y, a);                            // zero lag: forward, then backward through the result
    Vec fr(f.rbegin(), f.rend());
    fr = emaForward(fr, a);
    return Vec(fr.rbegin(), fr.rend());
}

Vec holt(const Vec &y, double a, double b) {
    const size_t m = y.size();
    Vec out(m);
    double level = y[0], trend = y[1] - y[0];
    out[0] = level;
    for (size_t i = 1; i < m; ++i) {
        const double prev = level;
        level = a * y[i] + (1 - a) * (prev + trend);
        trend = b * (level - prev) + (1 - b) * trend;
        out[i] = level;
    }
    return out;
}

// Savitzky-Golay: least squares polynomial in a sliding window of w points, evaluated at the centre. The window
// is moved inwards at the ends and the same polynomial is evaluated there (like scipy's mode="interp").
// Uses the real x values, so uneven spacing is handled correctly.
Vec savitzkyGolay(const Vec &x, const Vec &y, int w, int order) {
    const int m = int(y.size()), h = w / 2;
    Vec out(m);
    for (int i = 0; i < m; ++i) {
        const int l = std::max(0, std::min(i - h, m - w)), r = l + w - 1;
        const double c = x[i];
        double s = std::max(std::fabs(x[l] - c), std::fabs(x[r] - c));
        if (s <= 0) s = 1;
        std::vector<Vec> A(order + 1, Vec(order + 1, 0.0));
        Vec rhs(order + 1, 0.0);
        for (int j = l; j <= r; ++j) {
            const double t = (x[j] - c) / s;
            Vec pw(2 * order + 1, 1.0);
            for (int k = 1; k <= 2 * order; ++k) pw[k] = pw[k - 1] * t;
            for (int a = 0; a <= order; ++a) {
                rhs[a] += y[j] * pw[a];
                for (int b = 0; b <= order; ++b) A[a][b] += pw[a + b];
            }
        }
        if (solveDense(A, rhs)) out[i] = rhs[0];
        else {
            double sum = 0;
            for (int j = l; j <= r; ++j) sum += y[j];
            out[i] = sum / w;
        }
    }
    return out;
}

Vec medianFilter(const Vec &y, int w, Edge edge) {
    const int m = int(y.size()), h = w / 2;
    Vec out(m), buf;
    for (int i = 0; i < m; ++i) {
        buf.clear();
        for (int k = -h; k <= h; ++k) {
            const int j = mapIndex(i + k, m, edge);
            if (j >= 0) buf.push_back(y[j]);
        }
        out[i] = medianOf(buf);
    }
    return out;
}

Vec hampel(const Vec &y, int w, double nSigmas, Edge edge) {
    const int m = int(y.size()), h = w / 2;
    Vec out(m), buf, dev;
    for (int i = 0; i < m; ++i) {
        buf.clear();
        for (int k = -h; k <= h; ++k) {
            const int j = mapIndex(i + k, m, edge);
            if (j >= 0) buf.push_back(y[j]);
        }
        Vec copy = buf;
        const double med = medianOf(copy);
        dev.clear();
        for (double v : buf) dev.push_back(std::fabs(v - med));
        const double mad = 1.4826 * medianOf(dev);
        out[i] = std::fabs(y[i] - med) > nSigmas * mad ? med : y[i];
    }
    return out;
}

// Global outlier removal. Every rule gives an allowed range [lo, hi]; points outside are replaced.
Vec outlierRemoval(const Vec &x, const Vec &y, const Params &p) {
    const int m = int(y.size());
    double lo, hi;
    if (p.outlierRule == OutlierRule::ZScore) {
        double mean = std::accumulate(y.begin(), y.end(), 0.0) / m, var = 0;
        for (double v : y) var += (v - mean) * (v - mean);
        const double sd = std::sqrt(var / std::max(1, m - 1));
        lo = mean - p.threshold * sd;
        hi = mean + p.threshold * sd;
    } else if (p.outlierRule == OutlierRule::ModifiedZ) {
        Vec c = y;
        const double med = medianOf(c);
        Vec dev;
        for (double v : y) dev.push_back(std::fabs(v - med));
        double scale = 1.4826 * medianOf(dev);
        if (scale <= 0) {                                // more than half of the values equal: use the mean deviation
            double mad = std::accumulate(dev.begin(), dev.end(), 0.0) / m;
            scale = 1.2533 * mad;
        }
        lo = med - p.threshold * scale;
        hi = med + p.threshold * scale;
    } else {
        const double q1 = percentileOf(y, 0.25), q3 = percentileOf(y, 0.75);
        lo = q1 - p.threshold * (q3 - q1);
        hi = q3 + p.threshold * (q3 - q1);
    }
    Vec out = y;
    std::vector<char> bad(m, 0);
    for (int i = 0; i < m; ++i) bad[i] = (y[i] < lo || y[i] > hi);
    for (int i = 0; i < m; ++i) {
        if (!bad[i]) continue;
        if (p.replace == Replace::Clip) { out[i] = std::min(std::max(y[i], lo), hi); continue; }
        if (p.replace == Replace::Nan) { out[i] = kNaN; continue; }
        int a = i - 1, b = i + 1;
        while (a >= 0 && bad[a]) --a;
        while (b < m && bad[b]) ++b;
        if (a >= 0 && b < m) out[i] = y[a] + (y[b] - y[a]) * (x[i] - x[a]) / (x[b] - x[a]);
        else if (a >= 0) out[i] = y[a];
        else if (b < m) out[i] = y[b];
    }
    return out;
}

// LOWESS / LOESS: locally weighted polynomial regression (tricube weights, nearest `fraction` of the points),
// with optional robustness iterations (bisquare weights of the residuals) that remove the influence of outliers.
Vec lowess(const Vec &x, const Vec &y, double fraction, int degree, int robustIter) {
    const int m = int(y.size());
    int k = int(std::ceil(fraction * m));
    k = std::min(m, std::max(k, degree + 2));
    Vec fit(m), rw(m, 1.0);
    for (int it = 0; it <= robustIter; ++it) {
        int l = 0;
        for (int i = 0; i < m; ++i) {
            while (l + k < m && x[i] - x[l] > x[l + k] - x[i]) ++l;
            const int r = l + k - 1;
            const double h = std::max(x[i] - x[l], x[r] - x[i]);
            Vec wts(k);
            double wsum = 0, wy = 0;
            for (int j = l; j <= r; ++j) {
                double w = 1;
                if (h > 0) {
                    const double d = std::fabs(x[j] - x[i]) / h;
                    const double t = 1 - d * d * d;
                    w = d >= 1 ? 0 : t * t * t;
                }
                w *= rw[j];
                wts[j - l] = w;
                wsum += w;
                wy += w * y[j];
            }
            double value = wsum > 0 ? wy / wsum : y[i];
            if (degree > 0 && h > 0 && wsum > 0) {
                std::vector<Vec> A(degree + 1, Vec(degree + 1, 0.0));
                Vec rhs(degree + 1, 0.0);
                for (int j = l; j <= r; ++j) {
                    const double w = wts[j - l];
                    if (w <= 0) continue;
                    const double t = (x[j] - x[i]) / h;
                    Vec pw(2 * degree + 1, 1.0);
                    for (int q = 1; q <= 2 * degree; ++q) pw[q] = pw[q - 1] * t;
                    for (int a = 0; a <= degree; ++a) {
                        rhs[a] += w * y[j] * pw[a];
                        for (int b = 0; b <= degree; ++b) A[a][b] += w * pw[a + b];
                    }
                }
                if (solveDense(A, rhs)) value = rhs[0];
            }
            fit[i] = value;
        }
        if (it == robustIter) break;
        Vec res(m);
        for (int i = 0; i < m; ++i) res[i] = std::fabs(y[i] - fit[i]);
        Vec tmp = res;
        const double s = medianOf(tmp);
        for (int i = 0; i < m; ++i) {
            if (s <= 1e-300) { rw[i] = 1; continue; }
            const double u = res[i] / (6 * s);
            rw[i] = u < 1 ? (1 - u * u) * (1 - u * u) : 0;
        }
    }
    return fit;
}

// Whittaker-Eilers smoother: minimises sum (y - z)^2 + lambda * sum (d-th difference of z)^2.
// (I + lambda D'D) z = y is a symmetric banded system, solved with a banded Cholesky decomposition.
bool whittaker(const Vec &y, double lambda, int d, Vec &z, std::string &err) {
    const int m = int(y.size());
    if (m <= d) { err = "needs more points than the difference order"; return false; }
    Vec c(d + 1);                                        // coefficients of the d-th difference
    for (int k = 0; k <= d; ++k) {
        double binom = 1;
        for (int q = 1; q <= k; ++q) binom = binom * (d - q + 1) / q;
        c[k] = ((d - k) % 2 ? -1.0 : 1.0) * binom;
    }
    std::vector<Vec> A(m, Vec(d + 1, 0.0));              // A[i][s] = element (i, i+s)
    for (int i = 0; i < m; ++i) A[i][0] = 1.0;
    for (int r = 0; r + d < m; ++r)
        for (int a = 0; a <= d; ++a)
            for (int b = a; b <= d; ++b) A[r + a][b - a] += lambda * c[a] * c[b];
    // A = U'U, U upper triangular with the same band
    std::vector<Vec> U(m, Vec(d + 1, 0.0));
    for (int i = 0; i < m; ++i) {
        double diag = A[i][0];
        for (int k = std::max(0, i - d); k < i; ++k) diag -= U[k][i - k] * U[k][i - k];
        if (diag <= 0) { err = "the smoothing system is not positive definite"; return false; }
        U[i][0] = std::sqrt(diag);
        for (int s = 1; s <= d && i + s < m; ++s) {
            double v = A[i][s];
            for (int k = std::max(0, i + s - d); k < i; ++k) v -= U[k][i - k] * U[k][i + s - k];
            U[i][s] = v / U[i][0];
        }
    }
    Vec w(m);                                            // U' w = y
    for (int i = 0; i < m; ++i) {
        double v = y[i];
        for (int k = std::max(0, i - d); k < i; ++k) v -= U[k][i - k] * w[k];
        w[i] = v / U[i][0];
    }
    z.assign(m, 0.0);                                    // U z = w
    for (int i = m - 1; i >= 0; --i) {
        double v = w[i];
        for (int s = 1; s <= d && i + s < m; ++s) v -= U[i][s] * z[i + s];
        z[i] = v / U[i][0];
    }
    return true;
}

// Total variation denoising (Rudin-Osher-Fatemi): minimises 0.5 sum (y - z)^2 + lambda sum |z[i+1] - z[i]|.
// Solved in the dual with an accelerated projected gradient method (FISTA). Keeps steps and edges sharp.
Vec totalVariation(const Vec &y, double lambda, int iterations) {
    const int m = int(y.size());
    if (m < 2 || lambda <= 0) return y;
    Vec u(m - 1, 0.0), v(m - 1, 0.0), un(m - 1), x(m);
    double t = 1;
    auto primal = [&](const Vec &p, Vec &out) {
        out[0] = y[0] + p[0];
        for (int i = 1; i < m - 1; ++i) out[i] = y[i] - (p[i - 1] - p[i]);
        out[m - 1] = y[m - 1] - p[m - 2];
    };
    for (int it = 0; it < iterations; ++it) {
        primal(v, x);
        for (int i = 0; i < m - 1; ++i) un[i] = std::min(lambda, std::max(-lambda, v[i] + 0.25 * (x[i + 1] - x[i])));
        const double tn = 0.5 * (1 + std::sqrt(1 + 4 * t * t));
        const double mom = (t - 1) / tn;
        for (int i = 0; i < m - 1; ++i) v[i] = un[i] + mom * (un[i] - u[i]);
        u = un;
        t = tn;
    }
    primal(u, x);
    return x;
}

Vec bilateral(const Vec &y, int w, double sigmaS, double sigmaR, Edge edge) {
    const int m = int(y.size()), h = w / 2;
    if (sigmaR <= 0) sigmaR = std::max(noiseSigma(y), 1e-300) * 2.0;
    Vec out(m);
    for (int i = 0; i < m; ++i) {
        double s = 0, ws = 0;
        for (int k = -h; k <= h; ++k) {
            const int j = mapIndex(i + k, m, edge);
            if (j < 0) continue;
            const double dy = y[j] - y[i];
            const double wgt = std::exp(-0.5 * k * k / (sigmaS * sigmaS) - 0.5 * dy * dy / (sigmaR * sigmaR));
            s += wgt * y[j];
            ws += wgt;
        }
        out[i] = ws > 0 ? s / ws : y[i];
    }
    return out;
}

// Adaptive Wiener filter (like MATLAB's wiener2): smooths strongly where the local variance is about the noise
// variance, and leaves the signal where the local variance is much larger.
Vec wiener(const Vec &y, int w, double noiseVar, Edge edge) {
    const int m = int(y.size()), h = w / 2;
    Vec mean(m), var(m), out(m);
    for (int i = 0; i < m; ++i) {
        double s = 0, s2 = 0;
        int cnt = 0;
        for (int k = -h; k <= h; ++k) {
            const int j = mapIndex(i + k, m, edge);
            if (j < 0) continue;
            s += y[j];
            s2 += y[j] * y[j];
            ++cnt;
        }
        mean[i] = s / cnt;
        var[i] = std::max(0.0, s2 / cnt - mean[i] * mean[i]);
    }
    if (noiseVar <= 0) noiseVar = std::accumulate(var.begin(), var.end(), 0.0) / m;
    for (int i = 0; i < m; ++i)
        out[i] = var[i] > noiseVar ? mean[i] + (var[i] - noiseVar) / var[i] * (y[i] - mean[i]) : mean[i];
    return out;
}

// Local-level Kalman filter (random walk + noise), optionally with the Rauch-Tung-Striebel backward pass.
// Only the ratio q = process noise / measurement noise matters; small q = smooth, large q = follows the data.
Vec kalman(const Vec &x, const Vec &y, double q, bool smoother) {
    const int m = int(y.size());
    const double meanDx = (x[m - 1] - x[0]) / (m - 1);
    Vec xf(m), Pf(m), Pp(m);
    xf[0] = y[0];
    Pf[0] = 1.0;
    Pp[0] = 1.0;
    for (int k = 1; k < m; ++k) {
        const double dt = meanDx > 0 ? (x[k] - x[k - 1]) / meanDx : 1.0;
        Pp[k] = Pf[k - 1] + q * dt;
        const double K = Pp[k] / (Pp[k] + 1.0);
        xf[k] = xf[k - 1] + K * (y[k] - xf[k - 1]);
        Pf[k] = (1 - K) * Pp[k];
    }
    if (!smoother) return xf;
    Vec xs = xf;
    for (int k = m - 2; k >= 0; --k) {
        const double J = Pf[k] / Pp[k + 1];
        xs[k] = xf[k] + J * (xs[k + 1] - xf[k]);
    }
    return xs;
}

// ------------------------------------------------------------------------------------------------ even sampling

// Runs fn(y_on_even_grid, dx) and returns the result at the original x. Evenly spaced data is passed straight through.
template <class F> Vec onEvenGrid(const Vec &x, const Vec &y, F &&fn) {
    const int m = int(y.size());
    const double x0 = x[0], dx = (x[m - 1] - x[0]) / (m - 1);
    bool even = true;
    for (int i = 0; i < m && even; ++i) even = std::fabs(x[i] - (x0 + i * dx)) <= 1e-6 * dx;
    if (even) return fn(y, dx);
    Vec yu(m);
    int j = 0;
    for (int i = 0; i < m; ++i) {
        const double xu = x0 + i * dx;
        while (j + 2 < m && x[j + 1] <= xu) ++j;
        const double span = x[j + 1] - x[j];
        yu[i] = span > 0 ? y[j] + (y[j + 1] - y[j]) * std::min(1.0, std::max(0.0, (xu - x[j]) / span)) : y[j];
    }
    const Vec zu = fn(yu, dx);
    Vec z(m);
    for (int i = 0; i < m; ++i) {
        const double pos = std::min<double>(m - 1, std::max(0.0, (x[i] - x0) / dx));
        const int a = std::min(int(pos), m - 2);
        z[i] = zu[a] + (zu[a + 1] - zu[a]) * (pos - a);
    }
    return z;
}

// ------------------------------------------------------------------------------------------------ Butterworth

struct Sos { double b0, b1, b2, a1, a2; };

// Butterworth filter as second order sections (bilinear transform), cut-off wn as a fraction of the Nyquist frequency.
std::vector<Sos> butterSections(bool highPass, int order, double wn) {
    std::vector<Sos> secs;
    const double W = std::tan(kPi * wn / 2);
    for (int k = 0; k < order / 2; ++k) {
        const double zeta = std::sin(kPi * (2 * k + 1) / (2.0 * order));
        const double a0 = 1 + 2 * zeta * W + W * W;
        Sos s;
        s.a1 = 2 * (W * W - 1) / a0;
        s.a2 = (1 - 2 * zeta * W + W * W) / a0;
        if (highPass) { s.b0 = 1 / a0; s.b1 = -2 / a0; s.b2 = 1 / a0; }
        else { s.b0 = W * W / a0; s.b1 = 2 * W * W / a0; s.b2 = W * W / a0; }
        secs.push_back(s);
    }
    if (order % 2) {
        const double a0 = 1 + W;
        Sos s;
        s.a1 = (W - 1) / a0;
        s.a2 = 0;
        if (highPass) { s.b0 = 1 / a0; s.b1 = -1 / a0; }
        else { s.b0 = W / a0; s.b1 = W / a0; }
        s.b2 = 0;
        secs.push_back(s);
    }
    return secs;
}

// One pass through the cascade. The filter state starts at its steady state for the first sample, which removes the start-up transient.
void sosPass(Vec &data, const std::vector<Sos> &secs) {
    for (const Sos &s : secs) {
        const double G = (s.b0 + s.b1 + s.b2) / (1 + s.a1 + s.a2);
        const double x0 = data.empty() ? 0 : data[0];
        double z2 = (s.b2 - s.a2 * G) * x0;
        double z1 = (s.b1 - s.a1 * G) * x0 + z2;
        for (double &v : data) {
            const double in = v;
            const double out = s.b0 * in + z1;
            z1 = s.b1 * in - s.a1 * out + z2;
            z2 = s.b2 * in - s.a2 * out;
            v = out;
        }
    }
}

Vec sosFilter(const Vec &y, const std::vector<Sos> &secs, bool zeroPhase) {
    if (!zeroPhase) { Vec d = y; sosPass(d, secs); return d; }
    const int m = int(y.size());
    const int pad = std::min(m - 1, 3 * (2 * int(secs.size()) + 1));
    Vec ext;                                             // odd extension at both ends
    for (int i = pad; i >= 1; --i) ext.push_back(2 * y[0] - y[i]);
    ext.insert(ext.end(), y.begin(), y.end());
    for (int i = 1; i <= pad; ++i) ext.push_back(2 * y[m - 1] - y[m - 1 - i]);
    sosPass(ext, secs);
    std::reverse(ext.begin(), ext.end());
    sosPass(ext, secs);
    std::reverse(ext.begin(), ext.end());
    return Vec(ext.begin() + pad, ext.begin() + pad + m);
}

double cutoffToFraction(double c, CutoffUnit unit, double dx) {      // -> fraction of the Nyquist frequency
    return unit == CutoffUnit::FractionOfNyquist ? c : c * 2.0 * dx;
}

bool butterworth(const Vec &y, double dx, const Params &p, Vec &z, std::string &err) {
    const double w1 = cutoffToFraction(p.cutoff1, p.cutoffUnit, dx), w2 = cutoffToFraction(p.cutoff2, p.cutoffUnit, dx);
    const bool band = p.pass == PassType::BandPass || p.pass == PassType::BandStop;
    auto okFrac = [](double w) { return w > 0 && w < 1; };
    if (!okFrac(w1) || (band && !okFrac(w2))) {
        std::ostringstream s;
        s << "the cut-off must be between 0 and the Nyquist frequency (" << 0.5 / dx << " per X unit for this data)";
        err = s.str();
        return false;
    }
    if (band && !(w1 < w2)) { err = "the lower cut-off must be smaller than the upper cut-off"; return false; }
    switch (p.pass) {
    case PassType::LowPass: z = sosFilter(y, butterSections(false, p.order, w1), p.zeroPhase); break;
    case PassType::HighPass: z = sosFilter(y, butterSections(true, p.order, w1), p.zeroPhase); break;
    case PassType::BandPass: {
        std::vector<Sos> secs = butterSections(true, p.order, w1);              // high pass at the lower edge ...
        const std::vector<Sos> lp = butterSections(false, p.order, w2);          // ... then low pass at the upper edge
        secs.insert(secs.end(), lp.begin(), lp.end());
        z = sosFilter(y, secs, p.zeroPhase);
        break;
    }
    case PassType::BandStop: {                                                  // everything below w1 plus everything above w2
        const Vec a = sosFilter(y, butterSections(false, p.order, w1), p.zeroPhase);
        const Vec b = sosFilter(y, butterSections(true, p.order, w2), p.zeroPhase);
        z.resize(y.size());
        for (size_t i = 0; i < y.size(); ++i) z[i] = a[i] + b[i];
        break;
    }
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ FFT

void fftPow2(std::vector<cd> &a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * kPi / len * (inverse ? 1 : -1);
        const cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cd w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (cd &v : a) v /= double(n);
}

// DFT of any length (Bluestein's algorithm for lengths that are not a power of two).
void fft(std::vector<cd> &a, bool inverse) {
    const size_t n = a.size();
    if (n <= 1) return;
    if ((n & (n - 1)) == 0) { fftPow2(a, inverse); return; }
    if (inverse) {                                       // ifft(a) = conj(fft(conj(a))) / n
        for (cd &v : a) v = std::conj(v);
        fft(a, false);
        for (cd &v : a) v = std::conj(v) / double(n);
        return;
    }
    size_t M = 1;
    while (M < 2 * n - 1) M <<= 1;
    std::vector<cd> w(n), A(M, cd(0)), B(M, cd(0));
    for (size_t k = 0; k < n; ++k) {
        const double ang = -kPi * double((k * k) % (2 * n)) / double(n);
        w[k] = cd(std::cos(ang), std::sin(ang));
    }
    for (size_t k = 0; k < n; ++k) A[k] = a[k] * w[k];
    B[0] = std::conj(w[0]);
    for (size_t k = 1; k < n; ++k) B[k] = B[M - k] = std::conj(w[k]);
    fftPow2(A, false);
    fftPow2(B, false);
    for (size_t k = 0; k < M; ++k) A[k] *= B[k];
    fftPow2(A, true);
    for (size_t k = 0; k < n; ++k) a[k] = A[k] * w[k];
}

// Frequency filter. The (detrended) signal is mirrored at both ends first, which makes it periodic without a jump,
// so the filter does not ring from the ends.
bool fftFilter(const Vec &y, double dx, const Params &p, Vec &z, std::string &err) {
    const int m = int(y.size()), N = 2 * (m - 1);
    const double nyq = 0.5 / dx;
    auto toFreq = [&](double c) { return p.cutoffUnit == CutoffUnit::Frequency ? c : c * nyq; };
    const double f1 = toFreq(p.cutoff1), f2 = toFreq(p.cutoff2);
    const bool band = p.fftMode == FftMode::BandPass || p.fftMode == FftMode::BandStop;
    if (p.fftMode != FftMode::KeepStrongest) {
        if (!(f1 > 0 && f1 < nyq) || (band && !(f2 > 0 && f2 < nyq))) {
            std::ostringstream s;
            s << "the cut-off must be between 0 and the Nyquist frequency (" << nyq << " per X unit for this data)";
            err = s.str();
            return false;
        }
        if (band && !(f1 < f2)) { err = "the lower cut-off must be smaller than the upper cut-off"; return false; }
    }
    Vec trend(m);
    for (int i = 0; i < m; ++i) trend[i] = y[0] + (y[m - 1] - y[0]) * i / (m - 1);
    std::vector<cd> e(N);
    for (int i = 0; i < m; ++i) e[i] = y[i] - trend[i];
    for (int i = 1; i < m - 1; ++i) e[N - i] = e[i];
    fft(e, false);
    double maxAmp = 0;
    for (int k = 1; k < N; ++k) maxAmp = std::max(maxAmp, std::abs(e[k]));
    for (int k = 0; k < N; ++k) {
        const double f = std::min(k, N - k) / (N * dx);
        bool keep = true;
        switch (p.fftMode) {
        case FftMode::LowPass: keep = f <= f1; break;
        case FftMode::HighPass: keep = f >= f1; break;
        case FftMode::BandPass: keep = f >= f1 && f <= f2; break;
        case FftMode::BandStop: keep = f < f1 || f > f2; break;
        case FftMode::KeepStrongest: keep = k == 0 || std::abs(e[k]) >= p.keepPercent / 100.0 * maxAmp; break;
        }
        if (!keep) e[k] = 0;
    }
    fft(e, true);
    // The removed straight line is low-frequency content: it is put back only where low frequencies are kept.
    const bool keepTrend = p.fftMode != FftMode::HighPass && p.fftMode != FftMode::BandPass;
    z.resize(m);
    for (int i = 0; i < m; ++i) z[i] = e[i].real() + (keepTrend ? trend[i] : 0.0);
    return true;
}

// ------------------------------------------------------------------------------------------------ wavelets

const std::vector<std::pair<std::string, Vec>> &waveletBank() {
    static const std::vector<std::pair<std::string, Vec>> bank = {
        {"Haar", {0.7071067811865476, 0.7071067811865476}},
        {"Daubechies 2", {0.4829629131445342, 0.8365163037378079, 0.2241438680420134, -0.1294095225512604}},
        {"Daubechies 3", {0.3326705529500826, 0.8068915093110925, 0.4598775021184915, -0.1350110200102546,
                          -0.0854412738820267, 0.0352262918857095}},
        {"Daubechies 4", {0.2303778133088965, 0.7148465705529157, 0.6308807679298589, -0.0279837694168599,
                          -0.1870348117190931, 0.0308413818355608, 0.0328830116668852, -0.0105974017850690}},
        {"Daubechies 5", {0.1601023979741929, 0.6038292697971896, 0.7243085284377729, 0.1384281459013207,
                          -0.2422948870663820, -0.0322448695846384, 0.0775714938400457, -0.0062414902127983,
                          -0.0125807519990820, 0.0033357252854738}},
        {"Daubechies 6", {0.1115407433501095, 0.4946238903984531, 0.7511339080210954, 0.3152503517091976,
                          -0.2262646939654398, -0.1297668675672619, 0.0975016055873230, 0.0275228655303057,
                          -0.0315820393174860, 0.0005538422011615, 0.0047772575109455, -0.0010773010853085}},
        {"Daubechies 8", {0.0544158422431040, 0.3128715909142999, 0.6756307362972898, 0.5853546836542067,
                          -0.0158291052563493, -0.2840155429615469, 0.0004724845739133, 0.1287474266204785,
                          -0.0173693010018075, -0.0440882539307948, 0.0139810279173983, 0.0087460940474058,
                          -0.0048703529934516, -0.0003917403733769, 0.0006754494064506, -0.0001174767841248}},
    };
    return bank;
}

int maxWaveletLevels(int m, int filterLen) {
    int lv = 0;
    while ((m >> (lv + 1)) >= filterLen) ++lv;
    return lv;
}

bool waveletDenoise(const Vec &y, const Params &p, Vec &z, std::string &err) {
    const auto &bank = waveletBank();
    const Vec &lo = bank[std::min<size_t>(std::max(p.waveletIndex, 0), bank.size() - 1)].second;
    const int L = int(lo.size()), m = int(y.size());
    Vec hi(L);
    for (int j = 0; j < L; ++j) hi[j] = ((j % 2) ? -1.0 : 1.0) * lo[L - 1 - j];

    const int maxLv = maxWaveletLevels(m, L);
    if (maxLv < 1) { err = "too few points for this wavelet"; return false; }
    const int lv = p.levels <= 0 ? maxLv : std::min(p.levels, maxLv);
    const int block = 1 << lv;
    const int N = ((m + block - 1) / block) * block;
    Vec a(N);
    for (int i = 0; i < N; ++i) a[i] = y[mapIndex(i, m, Edge::Mirror)];   // mirrored at the end up to a multiple of 2^levels

    std::vector<Vec> details;
    for (int l = 0; l < lv; ++l) {
        const int n = int(a.size()), na = n / 2;
        Vec A(na, 0.0), D(na, 0.0);
        for (int k = 0; k < na; ++k)
            for (int j = 0; j < L; ++j) {
                const double v = a[(2 * k + j) % n];
                A[k] += lo[j] * v;
                D[k] += hi[j] * v;
            }
        details.push_back(D);
        a = A;
    }
    Vec fine;
    for (double v : details[0]) fine.push_back(std::fabs(v));
    const double sigma = medianOf(fine) / 0.6745;
    const double T = p.thresholdRule == ThresholdRule::Universal ? p.thresholdValue * sigma * std::sqrt(2.0 * std::log(double(N)))
                                                                 : p.thresholdValue;
    for (Vec &D : details)
        for (double &v : D) {
            const double mag = std::fabs(v);
            if (p.thresholdType == Threshold::Hard) { if (mag < T) v = 0; }
            else v = mag <= T ? 0.0 : (v > 0 ? mag - T : T - mag);
        }
    for (int l = lv - 1; l >= 0; --l) {
        const Vec &D = details[l];
        const int na = int(a.size()), n = 2 * na;
        Vec rec(n, 0.0);
        for (int k = 0; k < na; ++k)
            for (int j = 0; j < L; ++j) rec[(2 * k + j) % n] += lo[j] * a[k] + hi[j] * D[k];
        a = rec;
    }
    z.assign(a.begin(), a.begin() + m);
    return true;
}

// ------------------------------------------------------------------------------------------------ checks and names

std::string num(double v) {
    std::ostringstream s;
    s << v;
    return s.str();
}


bool validate(const Params &p, int m, std::string &err) {
    auto bad = [&](const char *msg) { err = msg; return false; };
    switch (p.method) {
    case Method::MovingAverage: case Method::Median: case Method::Hampel: case Method::Bilateral: case Method::Wiener:
        if (p.window < 1) return bad("the window must be at least 1 point");
        break;
    default: break;
    }
    switch (p.method) {
    case Method::Gaussian:
        if (!(p.sigma > 0)) return bad("sigma must be larger than 0");
        break;
    case Method::Exponential:
        if (!(p.alpha > 0 && p.alpha <= 1)) return bad("alpha must be larger than 0 and at most 1");
        break;
    case Method::Holt:
        if (!(p.alpha > 0 && p.alpha <= 1) || !(p.beta >= 0 && p.beta <= 1)) return bad("alpha must be in (0, 1] and beta in [0, 1]");
        if (m < 3) return bad("needs at least 3 points");
        break;
    case Method::SavitzkyGolay: {
        const int w = oddWindow(p.window, m);
        if (p.polyOrder < 0 || p.polyOrder >= w) {
            err = "the polynomial order (" + std::to_string(p.polyOrder) + ") must be smaller than the window (" + std::to_string(w) + " points)";
            return false;
        }
        break;
    }
    case Method::Hampel:
        if (!(p.nSigmas > 0)) return bad("the threshold (number of sigmas) must be larger than 0");
        break;
    case Method::Outlier:
        if (!(p.threshold > 0)) return bad("the threshold must be larger than 0");
        if (m < 4) return bad("needs at least 4 points");
        break;
    case Method::Lowess:
        if (!(p.fraction > 0 && p.fraction <= 1)) return bad("the fraction of points must be larger than 0 and at most 1");
        if (p.polyOrder < 0 || p.polyOrder > 2) return bad("the LOWESS degree must be 0, 1 or 2");
        if (m < p.polyOrder + 2) return bad("too few points for this degree");
        break;
    case Method::Whittaker:
        if (!(p.lambda >= 0)) return bad("lambda must not be negative");
        if (p.diffOrder < 1 || p.diffOrder > 6) return bad("the difference order must be between 1 and 6");
        break;
    case Method::TotalVariation:
        if (!(p.lambda >= 0)) return bad("lambda must not be negative");
        if (p.iterations < 1) return bad("the number of iterations must be at least 1");
        break;
    case Method::Bilateral:
        if (!(p.sigmaSpace > 0) || !(p.sigmaRange >= 0)) return bad("sigma (space) must be larger than 0 and sigma (range) must not be negative");
        break;
    case Method::Wiener:
        if (!(p.noiseVariance >= 0)) return bad("the noise variance must not be negative");
        break;
    case Method::Kalman:
        if (!(p.qRatio > 0)) return bad("the noise ratio must be larger than 0");
        break;
    case Method::Butterworth:
        if (p.order < 1 || p.order > 12) return bad("the filter order must be between 1 and 12");
        if (m < 8) return bad("needs at least 8 points");
        break;
    case Method::Fft:
        if (m < 4) return bad("needs at least 4 points");
        if (p.fftMode == FftMode::KeepStrongest && !(p.keepPercent >= 0 && p.keepPercent <= 100)) return bad("the percentage must be between 0 and 100");
        break;
    case Method::Wavelet:
        if (p.thresholdValue < 0) return bad("the threshold must not be negative");
        break;
    default: break;
    }
    return true;
}

} // namespace

const std::vector<std::string> &waveletNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (const auto &w : waveletBank()) n.push_back(w.first);
        return n;
    }();
    return names;
}

std::string describe(const Params &p) {
    std::ostringstream s;
    auto w = [&](int win) { return std::string("window ") + std::to_string(win % 2 ? win : win + 1); };
    switch (p.method) {
    case Method::MovingAverage: {
        static const char *names[] = {"moving average", "triangular average", "Hann average", "Hamming average", "Blackman average"};
        s << names[int(p.weights)] << ", " << w(p.window);
        break;
    }
    case Method::Gaussian: s << "Gaussian, sigma " << num(p.sigma); break;
    case Method::Exponential: {
        static const char *dir[] = {"forward", "backward", "zero-lag"};
        s << "exponential, alpha " << num(p.alpha) << ", " << dir[int(p.direction)];
        break;
    }
    case Method::Holt: s << "Holt, alpha " << num(p.alpha) << ", beta " << num(p.beta); break;
    case Method::SavitzkyGolay: s << "Savitzky-Golay, " << w(p.window) << ", order " << p.polyOrder; break;
    case Method::Median: s << "median, " << w(p.window); break;
    case Method::Hampel: s << "Hampel, " << w(p.window) << ", " << num(p.nSigmas) << " sigma"; break;
    case Method::Outlier: {
        static const char *rule[] = {"z-score", "modified z-score", "IQR"};
        s << "outliers removed, " << rule[int(p.outlierRule)] << " " << num(p.threshold);
        break;
    }
    case Method::Lowess: s << "LOWESS, fraction " << num(p.fraction) << ", degree " << p.polyOrder; break;
    case Method::Whittaker: s << "Whittaker, lambda " << num(p.lambda) << ", order " << p.diffOrder; break;
    case Method::TotalVariation: s << "total variation, lambda " << num(p.lambda); break;
    case Method::Bilateral: s << "bilateral, " << w(p.window) << ", sigma " << num(p.sigmaSpace); break;
    case Method::Wiener: s << "Wiener, " << w(p.window); break;
    case Method::Kalman: s << "Kalman, Q/R " << num(p.qRatio) << (p.smoother ? ", smoothed" : ", filter"); break;
    case Method::Butterworth: {
        static const char *t[] = {"low-pass", "high-pass", "band-pass", "band-stop"};
        const bool band = p.pass == PassType::BandPass || p.pass == PassType::BandStop;
        const bool frac = p.cutoffUnit == CutoffUnit::FractionOfNyquist;
        s << "Butterworth " << t[int(p.pass)] << ", order " << p.order << ", cut-off " << num(p.cutoff1);
        if (band) s << "-" << num(p.cutoff2);
        s << (frac ? " of Nyquist" : "");
        break;
    }
    case Method::Fft: {
        static const char *t[] = {"low-pass", "high-pass", "band-pass", "band-stop", "strongest components"};
        const bool band = p.fftMode == FftMode::BandPass || p.fftMode == FftMode::BandStop;
        const bool frac = p.cutoffUnit == CutoffUnit::FractionOfNyquist;
        s << "FFT " << t[int(p.fftMode)];
        if (p.fftMode == FftMode::KeepStrongest) s << " >" << num(p.keepPercent) << "%";
        else {
            s << ", cut-off " << num(p.cutoff1);
            if (band) s << "-" << num(p.cutoff2);
            s << (frac ? " of Nyquist" : "");
        }
        break;
    }
    case Method::Wavelet: {
        const auto &names = waveletNames();
        s << "wavelet " << names[std::min<size_t>(std::max(p.waveletIndex, 0), names.size() - 1)] << ", "
          << (p.thresholdType == Threshold::Soft ? "soft" : "hard");
        break;
    }
    }
    return s.str();
}

SamplingInfo samplingInfo(const std::vector<double> &x, const std::vector<double> &y) {
    std::vector<double> xs;
    for (size_t i = 0; i < x.size() && i < y.size(); ++i)
        if (std::isfinite(x[i]) && std::isfinite(y[i])) xs.push_back(x[i]);
    SamplingInfo info;
    info.points = int(xs.size());
    if (xs.size() < 2) return info;
    std::sort(xs.begin(), xs.end());
    const int m = int(xs.size());
    info.dx = (xs[m - 1] - xs[0]) / (m - 1);
    for (int i = 0; i < m && info.even; ++i) info.even = std::fabs(xs[i] - (xs[0] + i * info.dx)) <= 1e-6 * info.dx;
    return info;
}

bool filter(const std::vector<double> &x, const std::vector<double> &y, const Params &p,
            std::vector<double> &out, std::string &error) {
    const size_t n = y.size();
    out.assign(n, kNaN);
    if (x.size() != n) { error = "X and Y have different lengths"; return false; }
    std::vector<int> idx;
    for (size_t i = 0; i < n; ++i)
        if (std::isfinite(x[i]) && std::isfinite(y[i])) idx.push_back(int(i));
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return x[a] < x[b]; });
    const int m = int(idx.size());
    if (m < 3) { error = "needs at least 3 points with valid X and Y values"; return false; }
    Vec xs(m), ys(m);
    for (int k = 0; k < m; ++k) { xs[k] = x[idx[k]]; ys[k] = y[idx[k]]; }
    if (!validate(p, m, error)) return false;

    Vec z;
    switch (p.method) {
    case Method::MovingAverage: z = convolve(ys, windowWeights(p.weights, oddWindow(p.window, m) / 2), p.edge); break;
    case Method::Gaussian: {
        const int h = std::max(1, int(std::ceil(4 * p.sigma)));
        Vec wts(2 * h + 1);
        for (int k = -h; k <= h; ++k) wts[k + h] = std::exp(-0.5 * k * k / (p.sigma * p.sigma));
        z = convolve(ys, wts, p.edge);
        break;
    }
    case Method::Exponential: z = exponential(ys, p.alpha, p.direction); break;
    case Method::Holt: z = holt(ys, p.alpha, p.beta); break;
    case Method::SavitzkyGolay: z = savitzkyGolay(xs, ys, oddWindow(p.window, m), p.polyOrder); break;
    case Method::Median: z = medianFilter(ys, oddWindow(p.window, m), p.edge); break;
    case Method::Hampel: z = hampel(ys, oddWindow(p.window, m), p.nSigmas, p.edge); break;
    case Method::Outlier: z = outlierRemoval(xs, ys, p); break;
    case Method::Lowess: z = lowess(xs, ys, p.fraction, p.polyOrder, p.robustIterations); break;
    case Method::Whittaker: if (!whittaker(ys, p.lambda, p.diffOrder, z, error)) return false; break;
    case Method::TotalVariation: z = totalVariation(ys, p.lambda, p.iterations); break;
    case Method::Bilateral: z = bilateral(ys, oddWindow(p.window, m), p.sigmaSpace, p.sigmaRange, p.edge); break;
    case Method::Wiener: z = wiener(ys, oddWindow(p.window, m), p.noiseVariance, p.edge); break;
    case Method::Kalman: z = kalman(xs, ys, p.qRatio, p.smoother); break;
    case Method::Butterworth: {
        if (!(xs[m - 1] > xs[0])) { error = "all X values are equal"; return false; }
        std::string err;
        bool ok = true;
        z = onEvenGrid(xs, ys, [&](const Vec &yy, double dx) { Vec r; if (!butterworth(yy, dx, p, r, err)) ok = false; return r.empty() ? yy : r; });
        if (!ok) { error = err; return false; }
        break;
    }
    case Method::Fft: {
        if (!(xs[m - 1] > xs[0])) { error = "all X values are equal"; return false; }
        std::string err;
        bool ok = true;
        z = onEvenGrid(xs, ys, [&](const Vec &yy, double dx) { Vec r; if (!fftFilter(yy, dx, p, r, err)) ok = false; return r.empty() ? yy : r; });
        if (!ok) { error = err; return false; }
        break;
    }
    case Method::Wavelet: if (!waveletDenoise(ys, p, z, error)) return false; break;
    }
    for (int k = 0; k < m; ++k) out[idx[k]] = z[k];
    return true;
}

} // namespace nf
