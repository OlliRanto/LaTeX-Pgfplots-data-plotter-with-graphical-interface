#include "Plot3DWidget.h"

#include <QPainter>
#include <QMap>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLinearGradient>
#include <QPolygonF>
#include <QFontMetricsF>
#include <QFileInfo>
#include <QImage>
#include <QPdfWriter>
#include <QPageSize>
#include <QMarginsF>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const double kPi = 3.14159265358979323846;
const double kNaN = std::numeric_limits<double>::quiet_NaN();

struct CmapDef { const char *name; QVector<QColor> stops; };

const QVector<CmapDef> &cmapDefs() {
    static const QVector<CmapDef> defs = {
        {"Viridis",   {QColor(68, 1, 84), QColor(72, 40, 120), QColor(62, 74, 137), QColor(49, 104, 142), QColor(38, 130, 142),
                       QColor(31, 158, 137), QColor(53, 183, 121), QColor(109, 205, 89), QColor(180, 222, 44), QColor(253, 231, 37)}},
        {"Plasma",    {QColor(13, 8, 135), QColor(84, 2, 163), QColor(139, 10, 165), QColor(185, 50, 137), QColor(219, 92, 104),
                       QColor(244, 136, 73), QColor(254, 188, 43), QColor(240, 249, 33)}},
        {"Coolwarm",  {QColor(59, 76, 192), QColor(141, 176, 254), QColor(221, 221, 221), QColor(244, 154, 123), QColor(180, 4, 38)}},
        {"Rainbow",   {QColor(0, 0, 143), QColor(0, 0, 255), QColor(0, 255, 255), QColor(255, 255, 0), QColor(255, 0, 0), QColor(128, 0, 0)}},
        {"Terrain",   {QColor(51, 51, 153), QColor(0, 153, 255), QColor(102, 204, 102), QColor(240, 240, 120), QColor(153, 102, 51), QColor(255, 255, 255)}},
        {"Grayscale", {QColor(30, 30, 30), QColor(240, 240, 240)}},
    };
    return defs;
}

// "Nice" tick values inside [lo, hi] (steps of 1, 2 or 5 times a power of ten).
QVector<double> niceTicks(double lo, double hi, int target = 5) {
    QVector<double> out;
    const double range = hi - lo;
    if (!(range > 0) || !std::isfinite(range)) return out;
    const double raw = range / target;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    const double step = (norm < 1.5 ? 1.0 : norm < 3.5 ? 2.0 : norm < 7.5 ? 5.0 : 10.0) * mag;
    const double first = std::ceil(lo / step - 1e-9);
    for (int k = 0; k < 1000; ++k) {
        double v = (first + k) * step;
        if (v > hi + step * 1e-9) break;
        if (std::fabs(v) < step * 1e-9) v = 0;
        out << v;
    }
    return out;
}

QString tickText(double v, double range) {
    if (std::fabs(v) < range * 1e-9) return "0";
    return QString::number(v, 'g', 5);
}

QString texEscape(const QString &s) {
    QString o;
    for (QChar c : s) {
        switch (c.unicode()) {
        case '\\': o += "\\textbackslash{}"; break;
        case '&': case '%': case '$': case '#': case '_': case '{': case '}': o += QString("\\") + c; break;
        case '~': o += "\\textasciitilde{}"; break;
        case '^': o += "\\textasciicircum{}"; break;
        default: o += c;
        }
    }
    return o;
}

QString num(double v) { return QString::number(v, 'g', 8); }

// Translates an expression in the app's syntax (see ExprEval) into pgfmath. Every operation is written with
// its own parentheses, because the precedence differs: in pgfmath -x^2 is (-x)^2 and a^b^c is (a^b)^c, while
// the app (like Python) reads -(x^2) and a^(b^c). `vars` renames variables (the curve parameter t becomes x,
// the variable of pgfplots' domain). Returns false when the expression cannot be translated.
class PgfExprWriter {
public:
    PgfExprWriter(const QString &s, const QMap<QString, QString> &vars) : m_s(s), m_vars(vars) {}
    bool translate(QString &out) {
        try {
            QString r = expr();
            skipWs();
            if (m_pos != m_s.size()) return false;
            out = r;
            return true;
        } catch (...) { return false; }
    }
private:
    struct Fail {};
    QString m_s;
    QMap<QString, QString> m_vars;
    int m_pos = 0;

    void skipWs() { while (m_pos < m_s.size() && m_s[m_pos].isSpace()) ++m_pos; }
    QChar peek() { skipWs(); return m_pos < m_s.size() ? m_s[m_pos] : QChar(); }

    QString expr() {
        QString v = term();
        for (;;) {
            const QChar c = peek();
            if (c == '+' || c == '-') { ++m_pos; v = "(" + v + c + term() + ")"; }
            else break;
        }
        return v;
    }
    QString term() {
        QString v = unary();
        for (;;) {
            const QChar c = peek();
            if (c == '*' || c == '/') { ++m_pos; v = "(" + v + c + unary() + ")"; }
            else break;
        }
        return v;
    }
    QString unary() {
        const QChar c = peek();
        if (c == '-') { ++m_pos; return "(-" + unary() + ")"; }
        if (c == '+') { ++m_pos; return unary(); }
        return power();
    }
    QString power() {
        QString base = atom();
        if (peek() == '^') { ++m_pos; return "(" + base + "^" + unary() + ")"; }
        return base;
    }
    QString atom() {
        skipWs();
        if (m_pos >= m_s.size()) throw Fail();
        const QChar c = m_s[m_pos];
        if (c == '(') {
            ++m_pos;
            QString v = expr();
            if (peek() != ')') throw Fail();
            ++m_pos;
            return v;
        }
        if (c.isDigit() || c == '.') {
            const int start = m_pos;
            while (m_pos < m_s.size() && (m_s[m_pos].isDigit() || m_s[m_pos] == '.')) ++m_pos;
            if (m_pos < m_s.size() && (m_s[m_pos] == 'e' || m_s[m_pos] == 'E')) {
                const int save = m_pos++;
                if (m_pos < m_s.size() && (m_s[m_pos] == '+' || m_s[m_pos] == '-')) ++m_pos;
                if (m_pos < m_s.size() && m_s[m_pos].isDigit()) { while (m_pos < m_s.size() && m_s[m_pos].isDigit()) ++m_pos; }
                else m_pos = save;
            }
            bool ok = false;
            const double v = m_s.mid(start, m_pos - start).toDouble(&ok);
            if (!ok) throw Fail();
            return literal(v);
        }
        if (c.isLetter() || c == '_') {
            const int start = m_pos;
            while (m_pos < m_s.size() && (m_s[m_pos].isLetterOrNumber() || m_s[m_pos] == '_')) ++m_pos;
            const QString id = m_s.mid(start, m_pos - start);
            if (peek() == '(') {
                ++m_pos;
                QStringList args;
                args << expr();
                while (peek() == ',') { ++m_pos; args << expr(); }
                if (peek() != ')') throw Fail();
                ++m_pos;
                return call(id, args);
            }
            if (m_vars.contains(id)) return m_vars[id];
            if (id == "pi") return "3.14159265359";
            if (id == "e") return "2.71828182846";
            throw Fail();
        }
        throw Fail();
    }
    static QString literal(double v) {
        QString t = QString::number(v, 'f', 10);
        if (t.contains('.')) {
            while (t.endsWith('0')) t.chop(1);
            if (t.endsWith('.')) t.chop(1);
        }
        return t.isEmpty() ? QString("0") : t;
    }
    static QString call(const QString &f, const QStringList &a) {
        static const QStringList same = { "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh",
                                          "exp", "sqrt", "abs", "log10" };
        if (a.size() == 1) {
            if (same.contains(f)) return f + "(" + a[0] + ")";
            if (f == "log") return "ln(" + a[0] + ")";
            if (f == "log2") return "(ln(" + a[0] + ")/ln(2))";
        }
        if (a.size() == 2 && f == "power") return "pow(" + a[0] + "," + a[1] + ")";
        throw Fail();
    }
};

struct Prim { double depth; int kind; int idx; };   // kind: 0 surface cell, 1 line segment, 2 point

} // namespace

// ------------------------------------------------------------------ setup
Plot3DWidget::Plot3DWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
    setCursor(Qt::OpenHandCursor);
}

QStringList Plot3DWidget::colormapNames() {
    QStringList l;
    for (const CmapDef &d : cmapDefs()) l << d.name;
    return l;
}

QStringList Plot3DWidget::surfaceStyleNames() { return {"Filled", "Filled + mesh", "Mesh only"}; }

QColor Plot3DWidget::colormapColor(double t) const {
    const CmapDef &d = cmapDefs()[std::min(std::max(m_cmap, 0), int(cmapDefs().size()) - 1)];
    t = std::min(1.0, std::max(0.0, std::isfinite(t) ? t : 0.0));
    const int n = d.stops.size();
    const double pos = t * (n - 1);
    const int i = std::min(int(pos), n - 2);
    const double f = pos - i;
    const QColor &a = d.stops[i], &b = d.stops[i + 1];
    return QColor(int(a.red() + (b.red() - a.red()) * f + 0.5),
                  int(a.green() + (b.green() - a.green()) * f + 0.5),
                  int(a.blue() + (b.blue() - a.blue()) * f + 0.5));
}

// ------------------------------------------------------------------- data
void Plot3DWidget::clear() {
    m_nx = m_ny = 0;
    m_sx.clear(); m_sy.clear(); m_sz.clear();
    m_lx.clear(); m_ly.clear(); m_lz.clear();
    m_px.clear(); m_py.clear(); m_pz.clear();
    m_formula = Formula();
    rebuild();
    update();
}

void Plot3DWidget::setSurfaceFormula(const QString &zExpr, double x0, double x1, double y0, double y1, int samples) {
    m_formula = Formula();
    m_formula.kind = Formula::Surface;
    m_formula.ez = zExpr;
    m_formula.a0 = x0; m_formula.a1 = x1; m_formula.b0 = y0; m_formula.b1 = y1;
    m_formula.samples = samples;
}

void Plot3DWidget::setCurveFormula(const QString &xExpr, const QString &yExpr, const QString &zExpr, double t0, double t1, int samples) {
    m_formula = Formula();
    m_formula.kind = Formula::Curve;
    m_formula.ex = xExpr; m_formula.ey = yExpr; m_formula.ez = zExpr;
    m_formula.a0 = t0; m_formula.a1 = t1;
    m_formula.samples = samples;
}

void Plot3DWidget::setSurface(const QVector<double> &x, const QVector<double> &y, const QVector<double> &z) {
    if (x.size() < 2 || y.size() < 2 || z.size() != x.size() * y.size()) {
        m_nx = m_ny = 0; m_sx.clear(); m_sy.clear(); m_sz.clear();
    } else {
        m_nx = x.size(); m_ny = y.size();
        m_sx = x; m_sy = y; m_sz = z;
    }
    rebuild();
    update();
}

void Plot3DWidget::setLine(const QVector<double> &x, const QVector<double> &y, const QVector<double> &z) {
    const int n = std::min({x.size(), y.size(), z.size()});
    m_lx = x.mid(0, n); m_ly = y.mid(0, n); m_lz = z.mid(0, n);
    rebuild();
    update();
}

void Plot3DWidget::setPoints(const QVector<double> &x, const QVector<double> &y, const QVector<double> &z) {
    const int n = std::min({x.size(), y.size(), z.size()});
    m_px = x.mid(0, n); m_py = y.mid(0, n); m_pz = z.mid(0, n);
    rebuild();
    update();
}

bool Plot3DWidget::hasData() const { return hasSurface() || !m_lx.isEmpty() || !m_px.isEmpty(); }

bool Plot3DWidget::gridFromPoints(const QVector<double> &x, const QVector<double> &y, const QVector<double> &z,
                                  int resolution, QVector<double> &gx, QVector<double> &gy, QVector<double> &gz,
                                  bool *wasGrid) {
    const int N = std::min({x.size(), y.size(), z.size()});
    if (N < 3) return false;
    double xmin = std::numeric_limits<double>::infinity(), xmax = -xmin, ymin = xmin, ymax = -xmin;
    for (int k = 0; k < N; ++k) {
        xmin = std::min(xmin, x[k]); xmax = std::max(xmax, x[k]);
        ymin = std::min(ymin, y[k]); ymax = std::max(ymax, y[k]);
    }
    if (!(xmax > xmin) || !(ymax > ymin)) return false;
    const double ex = (xmax - xmin) * 1e-9, ey = (ymax - ymin) * 1e-9;

    auto uniq = [](QVector<double> v, double eps) {
        std::sort(v.begin(), v.end());
        QVector<double> u;
        for (double a : v)
            if (u.isEmpty() || a - u.last() > eps) u << a;
        return u;
    };
    const QVector<double> ux = uniq(x.mid(0, N), ex), uy = uniq(y.mid(0, N), ey);
    const qint64 cells = qint64(ux.size()) * uy.size();

    // 1) the samples already form a (possibly incomplete) regular grid
    if (ux.size() >= 2 && uy.size() >= 2 && cells <= qint64(2) * N && cells <= 250000) {
        gx = ux;
        gy = uy;
        gz = QVector<double>(int(cells), kNaN);
        auto find = [](const QVector<double> &u, double v, double eps) {
            return int(std::lower_bound(u.begin(), u.end(), v - eps) - u.begin());
        };
        for (int k = 0; k < N; ++k) {
            const int i = find(ux, x[k], ex), j = find(uy, y[k], ey);
            if (i < ux.size() && j < uy.size()) gz[j * ux.size() + i] = z[k];
        }
        if (wasGrid) *wasGrid = true;
        return true;
    }

    // 2) scattered samples: inverse-distance weighting onto a regular grid
    if (wasGrid) *wasGrid = false;
    const int res = std::min(400, std::max(8, resolution));
    QVector<double> sx, sy, sz;
    const int stride = std::max(1, int(std::ceil(N / 3000.0)));
    for (int k = 0; k < N; k += stride) { sx << x[k]; sy << y[k]; sz << z[k]; }
    const int M = sx.size();
    const double h = std::sqrt((xmax - xmin) * (ymax - ymin) / M);   // typical distance between samples
    const double maxD2 = 9.0 * h * h;                                // holes where no sample is within 3h
    const double tiny = 1e-20 * ((xmax - xmin) * (xmax - xmin) + (ymax - ymin) * (ymax - ymin));

    gx.resize(res); gy.resize(res);
    for (int i = 0; i < res; ++i) {
        gx[i] = xmin + (xmax - xmin) * i / (res - 1);
        gy[i] = ymin + (ymax - ymin) * i / (res - 1);
    }
    gz.resize(res * res);
    // Each node: weighted least-squares plane through its nearest samples (smoother than plain
    // inverse-distance weighting, which leaves a spike at every sample).
    const int K = std::min(M, 14);
    QVector<QPair<double, int>> dist(M);
    const double soft = 0.05 * h * h;
    for (int j = 0; j < res; ++j)
        for (int i = 0; i < res; ++i) {
            for (int k = 0; k < M; ++k) {
                const double dx = sx[k] - gx[i], dy = sy[k] - gy[j];
                dist[k] = qMakePair(dx * dx + dy * dy, k);
            }
            std::nth_element(dist.begin(), dist.begin() + (K - 1), dist.end());
            double best = std::numeric_limits<double>::infinity();
            int bestK = 0;
            for (int q = 0; q < K; ++q)
                if (dist[q].first < best) { best = dist[q].first; bestK = dist[q].second; }
            if (best <= tiny) { gz[j * res + i] = sz[bestK]; continue; }
            if (best > maxD2) { gz[j * res + i] = kNaN; continue; }
            double A[3][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
            double wsum = 0, zsum = 0;
            for (int q = 0; q < K; ++q) {
                const int k = dist[q].second;
                const double dx = sx[k] - gx[i], dy = sy[k] - gy[j];
                const double w = 1.0 / (dist[q].first + soft);
                const double f[3] = {1.0, dx, dy};
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 3; ++c) A[r][c] += w * f[r] * f[c];
                    A[r][3] += w * f[r] * sz[k];
                }
                wsum += w; zsum += w * sz[k];
            }
            // small ridge on the slopes keeps the system solvable for (nearly) collinear samples
            A[1][1] += 1e-9 * A[0][0] * h * h + 1e-300;
            A[2][2] += 1e-9 * A[0][0] * h * h + 1e-300;
            bool ok = true;
            for (int c = 0; c < 3 && ok; ++c) {          // Gaussian elimination with pivoting
                int piv = c;
                for (int r = c + 1; r < 3; ++r) if (std::fabs(A[r][c]) > std::fabs(A[piv][c])) piv = r;
                if (std::fabs(A[piv][c]) < 1e-300) { ok = false; break; }
                if (piv != c) for (int cc = 0; cc < 4; ++cc) std::swap(A[c][cc], A[piv][cc]);
                for (int r = c + 1; r < 3; ++r) {
                    const double m = A[r][c] / A[c][c];
                    for (int cc = c; cc < 4; ++cc) A[r][cc] -= m * A[c][cc];
                }
            }
            double v = zsum / wsum;                      // fallback: weighted mean
            if (ok) {
                double sol[3];
                for (int r = 2; r >= 0; --r) {
                    double acc = A[r][3];
                    for (int c = r + 1; c < 3; ++c) acc -= A[r][c] * sol[c];
                    sol[r] = acc / A[r][r];
                }
                if (std::isfinite(sol[0])) v = sol[0];   // the plane's value at the node itself
            }
            gz[j * res + i] = v;
        }
    return true;
}

// ------------------------------------------------------------------- look
void Plot3DWidget::setTitle(const QString &t) { m_title = t; update(); }
void Plot3DWidget::setAxisLabels(const QString &x, const QString &y, const QString &z) { m_xl = x; m_yl = y; m_zl = z; update(); }
void Plot3DWidget::setColormap(int index) { m_cmap = index; update(); }
void Plot3DWidget::setSurfaceStyle(int index) { m_surfStyle = index; update(); }
void Plot3DWidget::setShading(bool on) { m_shading = on; update(); }
void Plot3DWidget::setShowBox(bool on) { m_showBox = on; update(); }
void Plot3DWidget::setColorByZ(bool on) { m_colorByZ = on; update(); }
void Plot3DWidget::setLineColor(const QColor &c) { m_lineColor = c; update(); }
void Plot3DWidget::setLineWidth(double w) { m_lineWidth = w; update(); }
void Plot3DWidget::setPointSize(double s) { m_pointSize = s; update(); }
void Plot3DWidget::setEqualAspect(bool on) { m_equal = on; rebuild(); update(); }
void Plot3DWidget::setBoxHeight(double h) { m_boxHeight = std::max(0.05, h); rebuild(); update(); }
void Plot3DWidget::setZLimits(bool hasMin, double zmin, bool hasMax, double zmax) {
    m_hasZMin = hasMin; m_zMinFixed = zmin;
    m_hasZMax = hasMax; m_zMaxFixed = zmax;
    rebuild();
    update();
}

void Plot3DWidget::setView(double azimuthDeg, double elevationDeg) {
    m_az = azimuthDeg;
    m_el = std::min(90.0, std::max(-90.0, elevationDeg));
    update();
}

void Plot3DWidget::resetView() {
    m_az = -37.5; m_el = 30.0; m_zoom = 1.0; m_pan = QPointF();
    emit viewChanged(m_az, m_el);
    update();
}

// ------------------------------------------------------- derived geometry
void Plot3DWidget::updateRanges() {
    double xlo = std::numeric_limits<double>::infinity(), xhi = -xlo, ylo = xlo, yhi = -xlo, zlo = xlo, zhi = -xlo;
    auto add = [](double v, double &lo, double &hi) {
        if (!std::isfinite(v)) return;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    };
    if (hasSurface()) {
        for (double v : m_sx) add(v, xlo, xhi);
        for (double v : m_sy) add(v, ylo, yhi);
        for (double v : m_sz) add(v, zlo, zhi);
    }
    for (double v : m_lx) add(v, xlo, xhi);
    for (double v : m_ly) add(v, ylo, yhi);
    for (double v : m_lz) add(v, zlo, zhi);
    for (double v : m_px) add(v, xlo, xhi);
    for (double v : m_py) add(v, ylo, yhi);
    for (double v : m_pz) add(v, zlo, zhi);
    if (!(xhi >= xlo)) { xlo = 0; xhi = 1; }
    if (!(yhi >= ylo)) { ylo = 0; yhi = 1; }
    if (!(zhi >= zlo)) { zlo = 0; zhi = 1; }
    if (m_hasZMin) zlo = m_zMinFixed;
    if (m_hasZMax) zhi = m_zMaxFixed;
    if (!(zhi > zlo) && (m_hasZMin || m_hasZMax)) {      // contradicting limits: ignore them
        zlo = std::numeric_limits<double>::infinity(); zhi = -zlo;
        for (double v : m_sz) add(v, zlo, zhi);
        for (double v : m_lz) add(v, zlo, zhi);
        for (double v : m_pz) add(v, zlo, zhi);
        if (!(zhi >= zlo)) { zlo = 0; zhi = 1; }
    }
    auto fix = [](double &lo, double &hi) {
        if (!(hi - lo > 1e-12 * std::max(1.0, std::fabs(hi)))) { lo -= 0.5; hi += 0.5; }
    };
    fix(xlo, xhi); fix(ylo, yhi); fix(zlo, zhi);
    m_xlo = xlo; m_xhi = xhi; m_ylo = ylo; m_yhi = yhi; m_zlo = zlo; m_zhi = zhi;
}

void Plot3DWidget::rebuild() {
    updateRanges();
    const double rx = m_xhi - m_xlo, ry = m_yhi - m_ylo, rz = m_zhi - m_zlo;
    if (m_equal) {
        const double m = std::max({rx, ry, rz});
        m_dx = std::max(0.05, rx / m);
        m_dy = std::max(0.05, ry / m);
        m_dz = std::max(0.05, rz / m);
    } else {
        m_dx = m_dy = 1.0;
        m_dz = m_boxHeight;
    }
    const double eps = 1e-9 * rz;
    auto toBox = [&](double x, double y, double z) -> Vec3 {
        Vec3 v;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || z < m_zlo - eps || z > m_zhi + eps) {
            v.x = v.y = v.z = kNaN;
            return v;
        }
        v.x = ((x - m_xlo) / rx - 0.5) * m_dx;
        v.y = ((y - m_ylo) / ry - 0.5) * m_dy;
        v.z = ((z - m_zlo) / rz - 0.5) * m_dz;
        return v;
    };

    m_surfN.clear(); m_lineN.clear(); m_ptsN.clear();
    m_cellT.clear(); m_cellNx.clear(); m_cellNy.clear(); m_cellNz.clear();

    if (hasSurface()) {
        m_surfN.resize(m_nx * m_ny);
        for (int j = 0; j < m_ny; ++j)
            for (int i = 0; i < m_nx; ++i)
                m_surfN[j * m_nx + i] = toBox(m_sx[i], m_sy[j], m_sz[j * m_nx + i]);
        const int cx = m_nx - 1, cy = m_ny - 1;
        m_cellT.resize(cx * cy);
        m_cellNx.resize(cx * cy); m_cellNy.resize(cx * cy); m_cellNz.resize(cx * cy);
        for (int j = 0; j < cy; ++j)
            for (int i = 0; i < cx; ++i) {
                const int c = j * cx + i;
                const int k00 = j * m_nx + i, k10 = k00 + 1, k01 = k00 + m_nx, k11 = k01 + 1;
                const Vec3 &a = m_surfN[k00], &b = m_surfN[k10], &d = m_surfN[k01], &e = m_surfN[k11];
                if (std::isnan(a.x) || std::isnan(b.x) || std::isnan(d.x) || std::isnan(e.x)) {
                    m_cellT[c] = -1.f; m_cellNx[c] = m_cellNy[c] = 0.f; m_cellNz[c] = 1.f;
                    continue;
                }
                const double zavg = 0.25 * (m_sz[k00] + m_sz[k10] + m_sz[k01] + m_sz[k11]);
                m_cellT[c] = float(std::min(1.0, std::max(0.0, (zavg - m_zlo) / rz)));
                // facet normal = cross product of the two diagonals
                const double d1x = e.x - a.x, d1y = e.y - a.y, d1z = e.z - a.z;
                const double d2x = d.x - b.x, d2y = d.y - b.y, d2z = d.z - b.z;
                double nx = d1y * d2z - d1z * d2y, ny = d1z * d2x - d1x * d2z, nz = d1x * d2y - d1y * d2x;
                const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len < 1e-12) { nx = 0; ny = 0; nz = 1; } else { nx /= len; ny /= len; nz /= len; }
                m_cellNx[c] = float(nx); m_cellNy[c] = float(ny); m_cellNz[c] = float(nz);
            }
    }
    m_lineN.resize(m_lx.size());
    for (int i = 0; i < m_lx.size(); ++i) m_lineN[i] = toBox(m_lx[i], m_ly[i], m_lz[i]);
    m_ptsN.resize(m_px.size());
    for (int i = 0; i < m_px.size(); ++i) m_ptsN[i] = toBox(m_px[i], m_py[i], m_pz[i]);
}

// --------------------------------------------------------------- painting
void Plot3DWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    paintScene(p, QRectF(rect()));
}

void Plot3DWidget::paintScene(QPainter &p, const QRectF &area) const {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QFont fTick; fTick.setPixelSize(11);
    QFont fAxis; fAxis.setPixelSize(13);
    QFont fTitle; fTitle.setPixelSize(16); fTitle.setBold(true);

    if (!hasData()) {
        p.setPen(QColor(120, 120, 120));
        p.setFont(fAxis);
        p.drawText(area, Qt::AlignCenter, "No 3D plot yet \xe2\x80\x93 choose a source and press Plot");
        p.restore();
        return;
    }

    const bool haveLine = !m_lineN.isEmpty(), havePts = !m_ptsN.isEmpty(), haveSurf = hasSurface();
    const bool cbar = haveSurf || ((haveLine || havePts) && m_colorByZ);

    // ---- layout
    const double mTop = m_title.isEmpty() ? 10 : 40, mRight = cbar ? 84 : 10;
    const QRectF plotRect(area.left() + 10, area.top() + mTop, area.width() - 10 - mRight, area.height() - mTop - 10);

    // ---- projection (orthographic, view as in pgfplots)
    // pgfplots turns the scene the other way round: at view={-37.5}{30} the front corner is (xmin, ymin), x runs to the
    // right and y to the left. So the rotation angle is -azimuth.
    const double ar = -m_az * kPi / 180.0, er = m_el * kPi / 180.0;
    const double ca = std::cos(ar), sa = std::sin(ar), ce = std::cos(er), se = std::sin(er);
    const double R = 0.5 * std::sqrt(m_dx * m_dx + m_dy * m_dy + m_dz * m_dz);
    const double s = std::min(plotRect.width(), plotRect.height()) / (2.0 * R) * (m_showBox ? 0.72 : 0.92) * m_zoom;
    const QPointF c0 = plotRect.center() + m_pan;
    auto proj = [&](const Vec3 &v, double &depth) -> QPointF {
        const double xr = v.x * ca - v.y * sa;
        const double yr = v.x * sa + v.y * ca;
        depth = yr * ce - v.z * se;                    // larger = farther from the viewer
        return QPointF(c0.x() + xr * s, c0.y() - (v.z * ce + yr * se) * s);
    };
    auto projP = [&](const Vec3 &v) { double d; return proj(v, d); };
    auto shade = [](const QColor &c, double k) {
        return QColor(std::min(255, int(c.red() * k)), std::min(255, int(c.green() * k)), std::min(255, int(c.blue() * k)));
    };
    QVector<QRgb> lut(256);
    for (int i = 0; i < 256; ++i) lut[i] = colormapColor(i / 255.0).rgb();
    auto colAt = [&](double t) { return QColor(lut[int(std::lround(std::min(1.0, std::max(0.0, t)) * 255.0))]); };
    const double rz = m_zhi - m_zlo;

    // ---- title
    if (!m_title.isEmpty()) {
        p.setPen(Qt::black);
        p.setFont(fTitle);
        p.drawText(QRectF(area.left(), area.top() + 8, area.width(), 26), Qt::AlignCenter, m_title);
    }

    // ---- ticks (value and box position) of the three axes
    const double lo[3] = {m_xlo, m_ylo, m_zlo}, hi[3] = {m_xhi, m_yhi, m_zhi}, dd[3] = {m_dx, m_dy, m_dz};
    QVector<double> tv[3], tn[3];
    for (int a = 0; a < 3; ++a) {
        tv[a] = niceTicks(lo[a], hi[a], 5);
        for (double v : tv[a]) tn[a] << ((v - lo[a]) / (hi[a] - lo[a]) - 0.5) * dd[a];
    }
    auto make = [](int a, double va, int b, double vb, int c, double vc) {
        double v[3];
        v[a] = va; v[b] = vb; v[c] = vc;
        return Vec3{v[0], v[1], v[2]};
    };

    // ---- back walls with grid lines
    if (m_showBox) {
        for (int a = 0; a < 3; ++a)
            for (int side = 0; side < 2; ++side) {
                const int b = (a + 1) % 3, c = (a + 2) % 3;
                const double va = (side - 0.5) * dd[a];
                double dep;
                proj(make(a, va, b, 0, c, 0), dep);
                if (dep <= 1e-9) continue;                 // a wall in front of the data is not drawn
                QPolygonF face;
                const int ub[4] = {0, 1, 1, 0}, uc[4] = {0, 0, 1, 1};
                for (int k = 0; k < 4; ++k) face << projP(make(a, va, b, (ub[k] - 0.5) * dd[b], c, (uc[k] - 0.5) * dd[c]));
                p.setPen(QPen(QColor(160, 160, 166), 1.0));
                p.setBrush(QColor(245, 245, 248));
                p.drawPolygon(face);
                p.setPen(QPen(QColor(208, 208, 214), 0.8));
                for (double tb : tn[b])
                    p.drawLine(projP(make(a, va, b, tb, c, -dd[c] / 2)), projP(make(a, va, b, tb, c, dd[c] / 2)));
                for (double tc : tn[c])
                    p.drawLine(projP(make(a, va, b, -dd[b] / 2, c, tc)), projP(make(a, va, b, dd[b] / 2, c, tc)));
            }
        p.setBrush(Qt::NoBrush);
    }

    if (m_showBox) {
        // the edges of the box that face the viewer
        p.setPen(QPen(QColor(70, 70, 76), 1.0));
        for (int a = 0; a < 3; ++a) {
            const int b = (a + 1) % 3, c = (a + 2) % 3;
            for (int ib = 0; ib < 2; ++ib)
                for (int ic = 0; ic < 2; ++ic) {
                    const Vec3 v1 = make(a, -dd[a] / 2, b, (ib - 0.5) * dd[b], c, (ic - 0.5) * dd[c]);
                    const Vec3 v2 = make(a, dd[a] / 2, b, (ib - 0.5) * dd[b], c, (ic - 0.5) * dd[c]);
                    double d1, d2;
                    const QPointF a1 = proj(v1, d1), a2 = proj(v2, d2);
                    if (0.5 * (d1 + d2) < -1e-9) p.drawLine(a1, a2);
                }
        }

    }

    // ---- surface, line and points (far to near)
    QVector<QPointF> sp;
    QVector<double> sd;
    const int cx = m_nx - 1, cy = m_ny - 1;
    if (haveSurf) {
        sp.resize(m_surfN.size());
        sd.resize(m_surfN.size());
        for (int k = 0; k < m_surfN.size(); ++k) {
            if (std::isnan(m_surfN[k].x)) { sp[k] = QPointF(kNaN, kNaN); sd[k] = 0; }
            else sp[k] = proj(m_surfN[k], sd[k]);
        }
    }
    QVector<QPointF> lp(m_lineN.size()), pp(m_ptsN.size());
    QVector<double> ld(m_lineN.size()), pd(m_ptsN.size());
    for (int k = 0; k < m_lineN.size(); ++k) {
        if (std::isnan(m_lineN[k].x)) { lp[k] = QPointF(kNaN, kNaN); ld[k] = 0; } else lp[k] = proj(m_lineN[k], ld[k]);
    }
    for (int k = 0; k < m_ptsN.size(); ++k) {
        if (std::isnan(m_ptsN[k].x)) { pp[k] = QPointF(kNaN, kNaN); pd[k] = 0; } else pp[k] = proj(m_ptsN[k], pd[k]);
    }

    QPen linePen(m_lineColor, m_lineWidth);
    linePen.setCapStyle(Qt::RoundCap);
    linePen.setJoinStyle(Qt::RoundJoin);
    auto zT = [&](double z) { return (z - m_zlo) / rz; };
    auto drawSegment = [&](int i) {
        QPen pen = linePen;
        if (m_colorByZ) pen.setColor(colAt(zT(0.5 * (m_lz[i] + m_lz[i + 1]))));
        p.setPen(pen);
        p.drawLine(lp[i], lp[i + 1]);
    };
    auto drawPoint = [&](int i) {
        const QColor col = m_colorByZ ? colAt(zT(m_pz[i])) : m_lineColor;
        p.setPen(QPen(col.darker(150), 0.8));
        p.setBrush(col);
        p.drawEllipse(pp[i], m_pointSize * 0.5, m_pointSize * 0.5);
    };
    auto drawQuad = [&](int c) {
        const int j = c / cx, i = c % cx;
        const int k00 = j * m_nx + i, k10 = k00 + 1, k01 = k00 + m_nx, k11 = k01 + 1;
        const QPointF q[4] = {sp[k00], sp[k10], sp[k11], sp[k01]};
        const QColor base = colAt(m_cellT[c]);
        if (m_surfStyle == 2) {
            p.setPen(QPen(base, 1.0));
            p.setBrush(Qt::white);
        } else {
            QColor fill = base;
            if (m_shading) {
                const double nx = m_cellNx[c], ny = m_cellNy[c], nz = m_cellNz[c];
                const double nxr = nx * ca - ny * sa, nyr = nx * sa + ny * ca;
                const double nsy = nz * ce + nyr * se, ndep = nyr * ce - nz * se;
                // light from the viewer's upper left
                const double f = std::fabs(nxr * -0.40 + nsy * 0.60 + ndep * -0.69);
                fill = shade(base, 0.55 + 0.45 * std::min(1.0, f));
            }
            p.setBrush(fill);
            p.setPen(m_surfStyle == 1 ? QPen(QColor(0, 0, 0, 95), 0.6) : QPen(fill, 0.7));
        }
        p.drawPolygon(q, 4);
    };

    if (haveSurf) {
        QVector<Prim> prims;
        prims.reserve(size_t(cx) * size_t(cy) + size_t(m_lineN.size()) + size_t(m_ptsN.size()));
        for (int c = 0; c < cx * cy; ++c) {
            if (m_cellT[c] < 0) continue;
            const int j = c / cx, i = c % cx;
            const int k00 = j * m_nx + i, k10 = k00 + 1, k01 = k00 + m_nx, k11 = k01 + 1;
            prims.push_back({(sd[k00] + sd[k10] + sd[k01] + sd[k11]) * 0.25, 0, c});
        }
        for (int i = 0; i + 1 < m_lineN.size(); ++i)
            if (!std::isnan(lp[i].x()) && !std::isnan(lp[i + 1].x())) prims.push_back({0.5 * (ld[i] + ld[i + 1]) - 1e-3, 1, i});
        for (int i = 0; i < m_ptsN.size(); ++i)
            if (!std::isnan(pp[i].x())) prims.push_back({pd[i] - 1e-3, 2, i});
        std::sort(prims.begin(), prims.end(), [](const Prim &a, const Prim &b) { return a.depth > b.depth; });
        for (const Prim &pr : prims) {
            if (pr.kind == 0) drawQuad(pr.idx);
            else if (pr.kind == 1) drawSegment(pr.idx);
            else drawPoint(pr.idx);
        }
    } else {
        // no surface: the line is one stroke (fast) unless it is coloured by height
        if (haveLine) {
            if (m_colorByZ) {
                for (int i = 0; i + 1 < m_lineN.size(); ++i)
                    if (!std::isnan(lp[i].x()) && !std::isnan(lp[i + 1].x())) drawSegment(i);
            } else {
                p.setPen(linePen);
                p.setBrush(Qt::NoBrush);
                QPolygonF run;
                for (int i = 0; i < m_lineN.size(); ++i) {
                    if (std::isnan(lp[i].x())) {
                        if (run.size() > 1) p.drawPolyline(run);
                        run.clear();
                    } else {
                        run << lp[i];
                    }
                }
                if (run.size() > 1) p.drawPolyline(run);
            }
        }
        if (havePts) {
            // far points first so near ones stay visible
            QVector<int> order;
            for (int i = 0; i < m_ptsN.size(); ++i)
                if (!std::isnan(pp[i].x())) order << i;
            std::sort(order.begin(), order.end(), [&](int a, int b) { return pd[a] > pd[b]; });
            for (int i : order) drawPoint(i);
        }
    }
    p.setBrush(Qt::NoBrush);

    // ---- near box edges, tick marks and labels
    if (m_showBox) {
        auto corner = [&](int ix, int iy, int iz) { return Vec3{(ix - 0.5) * m_dx, (iy - 0.5) * m_dy, (iz - 0.5) * m_dz}; };
        auto midDepth = [&](const Vec3 &u, const Vec3 &v) { double d1, d2; proj(u, d1); proj(v, d2); return 0.5 * (d1 + d2); };
        const int iz0 = (m_el >= 0) ? 0 : 1;                    // the floor (the ceiling when seen from below)
        const int iyX = midDepth(corner(0, 0, iz0), corner(1, 0, iz0)) < midDepth(corner(0, 1, iz0), corner(1, 1, iz0)) ? 0 : 1;
        const int ixY = midDepth(corner(0, 0, iz0), corner(0, 1, iz0)) < midDepth(corner(1, 0, iz0), corner(1, 1, iz0)) ? 0 : 1;
        int ixZ = 0, iyZ = 0;
        double bestX = std::numeric_limits<double>::infinity();
        for (int ix = 0; ix < 2; ++ix)
            for (int iy = 0; iy < 2; ++iy) {
                const double px = projP(Vec3{(ix - 0.5) * m_dx, (iy - 0.5) * m_dy, 0}).x();
                if (px < bestX) { bestX = px; ixZ = ix; iyZ = iy; }
            }

        const QPointF centre = projP(Vec3{0, 0, 0});
        auto outward = [&](const Vec3 &mid) {
            QPointF o = projP(mid) - centre;
            const double len = std::hypot(o.x(), o.y());
            return len < 1e-6 ? QPointF(0, 1) : QPointF(o.x() / len, o.y() / len);
        };
        auto label = [&](const QPointF &pos, const QPointF &out, double gap, const QString &text, const QFont &f, bool rotate = false) {
            QFontMetricsF fm(f);
            const double tw = fm.horizontalAdvance(text) + 2, th = fm.height();
            QPointF cen = pos + out * gap;
            if (!rotate) cen += QPointF(out.x() * tw * 0.5, out.y() * th * 0.5);
            p.save();
            p.setFont(f);
            p.setPen(Qt::black);
            if (rotate) {
                p.translate(cen);
                p.rotate(-90);
                p.drawText(QRectF(-tw / 2, -th / 2, tw, th), Qt::AlignCenter, text);
            } else {
                p.drawText(QRectF(cen.x() - tw / 2, cen.y() - th / 2, tw, th), Qt::AlignCenter, text);
            }
            p.restore();
        };
        QFontMetricsF fmT(fTick);

        // X axis
        {
            const double yv = (iyX - 0.5) * m_dy, zv = (iz0 - 0.5) * m_dz;
            const QPointF out = outward(Vec3{0, yv, zv});
            for (int k = 0; k < tv[0].size(); ++k) {
                const QPointF pt = projP(Vec3{tn[0][k], yv, zv});
                p.setPen(QPen(QColor(70, 70, 76), 1.0));
                p.drawLine(pt, pt + out * 5);
                label(pt, out, 8, tickText(tv[0][k], hi[0] - lo[0]), fTick);
            }
            label(projP(Vec3{0, yv, zv}), out, 36, m_xl, fAxis);
        }
        // Y axis
        {
            const double xv = (ixY - 0.5) * m_dx, zv = (iz0 - 0.5) * m_dz;
            const QPointF out = outward(Vec3{xv, 0, zv});
            for (int k = 0; k < tv[1].size(); ++k) {
                const QPointF pt = projP(Vec3{xv, tn[1][k], zv});
                p.setPen(QPen(QColor(70, 70, 76), 1.0));
                p.drawLine(pt, pt + out * 5);
                label(pt, out, 8, tickText(tv[1][k], hi[1] - lo[1]), fTick);
            }
            label(projP(Vec3{xv, 0, zv}), out, 36, m_yl, fAxis);
        }
        // Z axis (on the left-most vertical edge)
        {
            const double xv = (ixZ - 0.5) * m_dx, yv = (iyZ - 0.5) * m_dy;
            const QPointF out(-1, 0);
            double maxW = 0;
            for (int k = 0; k < tv[2].size(); ++k) {
                const QString t = tickText(tv[2][k], hi[2] - lo[2]);
                maxW = std::max(maxW, fmT.horizontalAdvance(t));
                const QPointF pt = projP(Vec3{xv, yv, tn[2][k]});
                p.setPen(QPen(QColor(70, 70, 76), 1.0));
                p.drawLine(pt, pt + out * 5);
                label(pt, out, 8, t, fTick);
            }
            label(projP(Vec3{xv, yv, 0}), out, maxW + 26, m_zl, fAxis, true);
        }
    }

    // ---- colour bar
    if (cbar) {
        const double h = std::min(plotRect.height() * 0.6, 260.0);
        const QRectF bar(area.right() - 66, plotRect.center().y() - h / 2, 14, h);
        QLinearGradient grad(bar.bottomLeft(), bar.topLeft());
        for (int i = 0; i <= 10; ++i) grad.setColorAt(i / 10.0, colAt(i / 10.0));
        p.setPen(Qt::NoPen);
        p.fillRect(bar, QBrush(grad));
        p.setPen(QPen(QColor(70, 70, 76), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(bar);
        p.setFont(fTick);
        QFontMetricsF fm(fTick);
        for (double v : niceTicks(m_zlo, m_zhi, 5)) {
            const double y = bar.bottom() - (v - m_zlo) / rz * bar.height();
            p.drawLine(QPointF(bar.right(), y), QPointF(bar.right() + 4, y));
            p.drawText(QRectF(bar.right() + 6, y - fm.height() / 2, 60, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       tickText(v, m_zhi - m_zlo));
        }
    }
    p.restore();
}

// ------------------------------------------------------------ interaction
void Plot3DWidget::mousePressEvent(QMouseEvent *e) {
    m_lastMouse = e->position().toPoint();
    setCursor(e->button() == Qt::LeftButton ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
}

void Plot3DWidget::mouseMoveEvent(QMouseEvent *e) {
    const QPoint now = e->position().toPoint();
    const QPoint d = now - m_lastMouse;
    m_lastMouse = now;
    if (e->buttons() & Qt::LeftButton) {
        m_az -= d.x() * 0.5;                             // keeps the drag direction natural with the projection above
        m_el = std::min(90.0, std::max(-90.0, m_el + d.y() * 0.5));
        while (m_az > 180.0) m_az -= 360.0;
        while (m_az <= -180.0) m_az += 360.0;
        emit viewChanged(m_az, m_el);
        update();
    } else if (e->buttons() & (Qt::RightButton | Qt::MiddleButton)) {
        m_pan += QPointF(d);
        update();
    }
}

void Plot3DWidget::mouseReleaseEvent(QMouseEvent *) { setCursor(Qt::OpenHandCursor); }

void Plot3DWidget::mouseDoubleClickEvent(QMouseEvent *) { resetView(); }

void Plot3DWidget::wheelEvent(QWheelEvent *e) {
    m_zoom = std::min(10.0, std::max(0.2, m_zoom * std::pow(1.0015, e->angleDelta().y())));
    update();
    e->accept();
}

// ----------------------------------------------------------------- output
bool Plot3DWidget::saveImage(const QString &path, QString *error) const {
    const QString ext = QFileInfo(path).suffix().toLower();
    QSizeF sz(size());
    if (sz.width() < 50 || sz.height() < 50) sz = QSizeF(800, 600);

    if (ext == "pdf") {
        QPdfWriter w(path);
        w.setResolution(300);
        w.setPageMargins(QMarginsF(0, 0, 0, 0));
        const double mmW = 170.0;
        w.setPageSize(QPageSize(QSizeF(mmW, mmW * sz.height() / sz.width()), QPageSize::Millimeter));
        QPainter p(&w);
        if (!p.isActive()) {
            if (error) *error = "Could not write the PDF file.";
            return false;
        }
        const double scale = double(w.width()) / sz.width();
        p.scale(scale, scale);
        p.fillRect(QRectF(QPointF(0, 0), sz), Qt::white);
        paintScene(p, QRectF(QPointF(0, 0), sz));
        p.end();
        return true;
    }
    QImage img((sz * 2.0).toSize(), QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.scale(2.0, 2.0);
    paintScene(p, QRectF(QPointF(0, 0), sz));
    p.end();
    const bool ok = (ext == "jpg" || ext == "jpeg") ? img.save(path, nullptr, 95) : img.save(path);
    if (!ok && error) *error = "Could not write the image file.";
    return ok;
}

Plot3DLatexResult Plot3DWidget::toPgfplots(const Plot3DLatexOptions &opts) const {
    Plot3DLatexResult result;
    QStringList o;
    const bool haveSurf = hasSurface(), haveLine = !m_lx.isEmpty(), havePts = !m_px.isEmpty();
    const bool cbar = haveSurf || ((haveLine || havePts) && m_colorByZ);
    const QString lineRgb = QString("{rgb,255:red,%1;green,%2;blue,%3}").arg(m_lineColor.red()).arg(m_lineColor.green()).arg(m_lineColor.blue());

    // ---- CSV helpers
    QString sepChar = ",";
    if (opts.colSep == "semicolon") sepChar = ";";
    else if (opts.colSep == "tab") sepChar = "\t";
    else if (opts.colSep == "space") sepChar = " ";
    const QString sepName = (opts.colSep == "semicolon" || opts.colSep == "tab" || opts.colSep == "space") ? opts.colSep : QString("comma");
    QString base;
    for (QChar c : opts.csvBaseName.trimmed())
        base += (c.isLetterOrNumber() || c == '_' || c == '-') ? c : QChar('_');
    if (base.isEmpty()) base = "figure";
    auto csvNum = [](double v) { return std::isfinite(v) ? QString::number(v, 'g', 10) : QString("nan"); };
    auto tableRef = [&](const QString &suffix) {
        return QString("table[col sep=%1, x=x, y=y, z=z] {%2_%3.csv}").arg(sepName, base, suffix);
    };

    // Data limits. pdfTeX (the "latex" engine of the SVG route) runs out of memory with surfaces much above
    // 60 x 60 faces, so embedded data is always thinned; CSV data is complete except for the SVG route.
    const bool thin = !opts.useCsv || opts.forSvg;
    const int maxGrid = 60, maxPts = 2000;

    // ---- document head
    o << "% Created by Universal Plotter (3D plot)";
    if (opts.wrapFigure) {
        o << "\\documentclass{article}" << "\\usepackage[margin=1in]{geometry}";
        // the [H] ("put it exactly here") placement comes from the float package
        if (opts.figurePlacement.contains('H')) o << "\\usepackage{float}";
        o << "\\usepackage{pgfplots}" << "\\pgfplotsset{compat=1.14}"
          << "\\begin{document}"
          << QString("\\begin{figure}[%1]").arg(opts.figurePlacement.isEmpty() ? QString("htbp") : opts.figurePlacement)
          << "    \\centering";
    } else {
        o << "\\documentclass[border=8pt]{standalone}" << "\\usepackage{pgfplots}" << "\\pgfplotsset{compat=1.14}"
          << "\\begin{document}";
    }
    if (opts.forSvg) {
        // dvisvgm: the SVG driver opens a <g fill=...> group for every colour change and closes it only at the end
        // of the pgf scope, so a surface of N faces would be N groups deep and dvisvgm overflows its stack.
        // Drawing every face in a scope of its own closes its group at once.
        o << "\\makeatletter"
          << "\\@ifundefined{pgfplotsplothandlermesh@VISUALIZE@std@fill@andor@stroke}{}{%"
          << "  \\let\\uplotterVisualizeOrig\\pgfplotsplothandlermesh@VISUALIZE@std@fill@andor@stroke"
          << "  \\def\\pgfplotsplothandlermesh@VISUALIZE@std@fill@andor@stroke{\\pgfscope\\uplotterVisualizeOrig\\endpgfscope}}"
          << "\\makeatother";
    }
    o << "\\begin{tikzpicture}" << "\\begin{axis}[";
    QStringList ax;
    auto esc = [&opts](const QString &t) { return opts.escapeText ? texEscape(t) : t; };
    if (!m_title.isEmpty()) ax << QString("title={%1}").arg(esc(m_title));
    ax << QString("xlabel={%1}").arg(esc(m_xl)) << QString("ylabel={%1}").arg(esc(m_yl))
       << QString("zlabel={%1}").arg(esc(m_zl));
    ax << QString("view={%1}{%2}").arg(num(m_az)).arg(num(m_el));
    ax << QString("xmin=%1, xmax=%2").arg(num(m_xlo), num(m_xhi))
       << QString("ymin=%1, ymax=%2").arg(num(m_ylo), num(m_yhi))
       << QString("zmin=%1, zmax=%2").arg(num(m_zlo), num(m_zhi));
    if (m_equal) ax << "unit vector ratio*=1 1 1";
    ax << QString("width=%1cm").arg(num(opts.widthCm)) << "grid=major" << "unbounded coords=jump";
    {   // colour map: the same stops as on screen
        const CmapDef &d = cmapDefs()[std::min(std::max(m_cmap, 0), int(cmapDefs().size()) - 1)];
        QStringList st;
        for (int i = 0; i < d.stops.size(); ++i)
            st << QString("rgb255(%1cm)=(%2,%3,%4)").arg(i).arg(d.stops[i].red()).arg(d.stops[i].green()).arg(d.stops[i].blue());
        ax << QString("colormap={plotter}{%1}").arg(st.join(' '));
    }
    if (cbar) ax << "colorbar" << "colorbar style={width=3mm}";
    for (int i = 0; i < ax.size(); ++i) o << "  " + ax[i] + (i + 1 < ax.size() ? "," : "");
    o << "]";

    auto coord = [&](double x, double y, double z) {
        return QString("(%1,%2,%3)").arg(num(x), num(y), std::isfinite(z) ? num(z) : QString("nan"));
    };
    const QString header = QString("x%1y%1z").arg(sepChar);

    // ---- surface
    if (haveSurf) {
        const QString style = m_surfStyle == 2 ? QString("mesh") : QString("surf");
        const QString extra = m_surfStyle == 1 ? ", draw=black!60, line width=0.2pt" : (m_surfStyle == 0 ? ", shader=flat" : "");
        QString fz;
        const bool formulaSurf = opts.useFormula && m_formula.kind == Formula::Surface
                                 && PgfExprWriter(m_formula.ez, {{"x", "x"}, {"y", "y"}}).translate(fz);
        if (formulaSurf) {
            // the formula itself, not its values: pgfplots calculates the grid when the document is compiled
            int n = opts.formulaSamples > 1 ? opts.formulaSamples : m_formula.samples;
            n = std::max(2, opts.forSvg ? std::min(n, maxGrid) : n);
            const double span = std::max(m_zhi - m_zlo, 1e-12);
            // values far outside the axis range would overflow TeX: cut them (a hole, e.g. at a pole)
            o << QString("\\addplot3[%1%2, domain=%3:%4, y domain=%5:%6, samples=%7, samples y=%7, trig format plots=rad,")
                     .arg(style, extra, num(m_formula.a0), num(m_formula.a1), num(m_formula.b0), num(m_formula.b1)).arg(n)
              << QString("    restrict z to domain=%1:%2]").arg(num(m_zlo - 10 * span), num(m_zhi + 10 * span))
              << QString("    {%1};").arg(fz);
        } else if (opts.useCsv) {
            QStringList rows;
            rows << header;
            const int csx = thin ? std::max(1, int(std::ceil(m_nx / double(maxGrid)))) : 1;
            const int csy = thin ? std::max(1, int(std::ceil(m_ny / double(maxGrid)))) : 1;
            for (int j = 0; j < m_ny; j += csy) {
                for (int i = 0; i < m_nx; i += csx)
                    rows << csvNum(m_sx[i]) + sepChar + csvNum(m_sy[j]) + sepChar + csvNum(m_sz[j * m_nx + i]);
                rows << "";                                   // blank line = end of a scanline
            }
            result.csvFiles[base + "_surface.csv"] = rows.join('\n');
            o << QString("\\addplot3[%1%2] %3;").arg(style, extra, tableRef("surface"));
        } else {
            // very fine grids are thinned so that TeX does not run out of memory
            const int sx = std::max(1, int(std::ceil(m_nx / double(maxGrid)))), sy = std::max(1, int(std::ceil(m_ny / double(maxGrid))));
            if (sx > 1 || sy > 1) o << "% grid thinned to at most 60 x 60 points (use the CSV option for all points)";
            o << QString("\\addplot3[%1%2] coordinates {").arg(style, extra);
            for (int j = 0; j < m_ny; j += sy) {
                QStringList row;
                for (int i = 0; i < m_nx; i += sx) row << coord(m_sx[i], m_sy[j], m_sz[j * m_nx + i]);
                o << "  " + row.join(' ');
                o << "";                                      // blank line = end of a scanline
            }
            o << "};";
        }
    }
    // ---- line
    if (haveLine) {
        const QString opt = m_colorByZ ? QString("mesh, line width=%1pt").arg(num(m_lineWidth * 0.75))
                                       : QString("color=%1, line width=%2pt").arg(lineRgb).arg(num(m_lineWidth * 0.75));
        QString fx, fy, fz;
        const QMap<QString, QString> tv = {{"t", "x"}};          // pgfplots' sample variable is x
        const bool formulaCurve = opts.useFormula && m_formula.kind == Formula::Curve
                                  && PgfExprWriter(m_formula.ex, tv).translate(fx)
                                  && PgfExprWriter(m_formula.ey, tv).translate(fy)
                                  && PgfExprWriter(m_formula.ez, tv).translate(fz);
        if (formulaCurve) {
            int n = opts.formulaSamples > 1 ? opts.formulaSamples : m_formula.samples;
            n = std::max(2, opts.forSvg ? std::min(n, maxPts) : n);
            o << QString("\\addplot3[%1, domain=%2:%3, samples=%4, samples y=0, trig format plots=rad]")
                     .arg(opt, num(m_formula.a0), num(m_formula.a1)).arg(n)
              << QString("    ({%1}, {%2}, {%3});").arg(fx, fy, fz);
        } else if (opts.useCsv) {
            QStringList rows;
            rows << header;
            const int cst = thin ? std::max(1, int(std::ceil(m_lx.size() / double(maxPts)))) : 1;
            for (int i = 0; i < m_lx.size(); i += cst)
                if (std::isfinite(m_lx[i]) && std::isfinite(m_ly[i]) && std::isfinite(m_lz[i]))
                    rows << csvNum(m_lx[i]) + sepChar + csvNum(m_ly[i]) + sepChar + csvNum(m_lz[i]);
            result.csvFiles[base + "_line.csv"] = rows.join('\n');
            o << QString("\\addplot3[%1] %2;").arg(opt, tableRef("line"));
        } else {
            const int st = std::max(1, int(std::ceil(m_lx.size() / double(maxPts))));
            if (st > 1) o << "% line thinned to about 2000 points (use the CSV option for all points)";
            o << QString("\\addplot3[%1] coordinates {").arg(opt);
            for (int i = 0; i < m_lx.size(); i += st)
                if (std::isfinite(m_lx[i]) && std::isfinite(m_ly[i]) && std::isfinite(m_lz[i])) o << "  " + coord(m_lx[i], m_ly[i], m_lz[i]);
            o << "};";
        }
    }
    // ---- points
    if (havePts) {
        const QString opt = QString("%1, only marks, mark=*, mark size=%2pt")
                                .arg(m_colorByZ ? QString("scatter") : QString("color=%1").arg(lineRgb)).arg(num(m_pointSize * 0.4));
        if (opts.useCsv) {
            QStringList rows;
            rows << header;
            const int cst = thin ? std::max(1, int(std::ceil(m_px.size() / double(maxPts)))) : 1;
            for (int i = 0; i < m_px.size(); i += cst)
                if (std::isfinite(m_px[i]) && std::isfinite(m_py[i]) && std::isfinite(m_pz[i]))
                    rows << csvNum(m_px[i]) + sepChar + csvNum(m_py[i]) + sepChar + csvNum(m_pz[i]);
            result.csvFiles[base + "_points.csv"] = rows.join('\n');
            o << QString("\\addplot3[%1] %2;").arg(opt, tableRef("points"));
        } else {
            const int st = std::max(1, int(std::ceil(m_px.size() / double(maxPts))));
            if (st > 1) o << "% points thinned to about 2000 (use the CSV option for all points)";
            o << QString("\\addplot3[%1] coordinates {").arg(opt);
            for (int i = 0; i < m_px.size(); i += st)
                if (std::isfinite(m_px[i]) && std::isfinite(m_py[i]) && std::isfinite(m_pz[i])) o << "  " + coord(m_px[i], m_py[i], m_pz[i]);
            o << "};";
        }
    }
    o << "\\end{axis}" << "\\end{tikzpicture}";
    if (opts.wrapFigure) {
        if (!opts.caption.trimmed().isEmpty()) o << QString("    \\caption{%1}").arg(esc(opts.caption.trimmed()));
        if (!opts.figLabel.trimmed().isEmpty()) o << QString("    \\label{%1}").arg(opts.figLabel.trimmed());
        o << "\\end{figure}";
    }
    o << "\\end{document}" << "";
    result.code = o.join('\n');
    return result;
}
