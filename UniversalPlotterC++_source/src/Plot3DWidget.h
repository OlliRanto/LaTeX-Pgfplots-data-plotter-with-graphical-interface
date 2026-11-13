#pragma once
#include <QWidget>
#include <QVector>
#include <QColor>
#include <QString>
#include <QStringList>
#include <QPointF>
#include <QPoint>

#include <QMap>

class QPainter;

// What the LaTeX export of a 3D plot should look like.
struct Plot3DLatexOptions {
    bool useFormula = false;             // plots made from an expression: write the formula (\addplot3 {f(x,y)}) instead of data
    int formulaSamples = 0;              // samples of the formula (per axis for a surface); 0 = as in the plot
    bool useCsv = false;                 // false: data is embedded in the code; true: loaded from CSV file(s)
    QString csvBaseName = "figure";      // CSV files are <name>_surface.csv, <name>_line.csv, <name>_points.csv
    QString colSep = "comma";            // comma, semicolon, tab or space
    double widthCm = 11.0;
    bool wrapFigure = false;             // false: standalone document; true: article with a figure environment
    QString caption, figLabel, figurePlacement = "htbp";   // as in the 2D export
    bool escapeText = false;             // add backslashes to LaTeX special characters in title, labels and caption
    bool forSvg = false;                 // for the latex -> dvisvgm route: shallow SVG nesting and limited size (see toPgfplots)
};

struct Plot3DLatexResult {
    QString code;                        // the .tex document
    QMap<QString, QString> csvFiles;     // file name -> content (only with useCsv)
};

// A small software 3D plot (QPainter only -- no OpenGL / Qt DataVisualization needed, so it
// also works in plain remote / VM sessions and renders identically to PNG / PDF).
//
// One scene holds at most one surface, one polyline and one point cloud. Drawing uses the
// painter's algorithm (far to near), which is exact enough for surfaces and curves.
//
// View convention (the same as pgfplots' view={azimuth}{elevation}): azimuth turns the scene
// about the vertical axis, elevation tilts it; the default is -37.5 / 30.
//
//   drag            rotate
//   right drag      pan
//   wheel           zoom
//   double click    reset the view
class Plot3DWidget : public QWidget {
    Q_OBJECT
public:
    explicit Plot3DWidget(QWidget *parent = nullptr);

    // ---- data ----
    void clear();
    // Surface on the grid x[0..nx-1] by y[0..ny-1]; z is row-major by y: z[j*nx + i]. NaN = hole.
    void setSurface(const QVector<double> &x, const QVector<double> &y, const QVector<double> &z);
    void setLine(const QVector<double> &x, const QVector<double> &y, const QVector<double> &z);
    void setPoints(const QVector<double> &x, const QVector<double> &y, const QVector<double> &z);
    bool hasData() const;
    bool hasSurface() const { return m_nx >= 2 && m_ny >= 2; }

    // The expression a surface / curve was calculated from. Only used by the LaTeX export, which then
    // writes the formula (like the 2D export does for plotted functions) instead of a list of points.
    // Call after setSurface() / setLine(); clear() forgets it. Expressions use the app's syntax.
    void setSurfaceFormula(const QString &zExpr, double x0, double x1, double y0, double y1, int samples);
    void setCurveFormula(const QString &xExpr, const QString &yExpr, const QString &zExpr, double t0, double t1, int samples);
    bool hasFormula() const { return m_formula.kind != Formula::None; }
    int formulaSamples() const { return m_formula.samples; }
    bool formulaIsSurface() const { return m_formula.kind == Formula::Surface; }

    // Turns scattered (x, y, z) samples into a regular grid for setSurface(). A file that already
    // is a grid (every x combined with every y) is used as it is; anything else is interpolated
    // (local weighted plane fit) onto resolution x resolution nodes, leaving holes where no
    // sample is near. Returns false when there is not enough data.
    static bool gridFromPoints(const QVector<double> &x, const QVector<double> &y, const QVector<double> &z,
                               int resolution, QVector<double> &gx, QVector<double> &gy, QVector<double> &gz,
                               bool *wasGrid = nullptr);

    // ---- text ----
    void setTitle(const QString &t);
    void setAxisLabels(const QString &x, const QString &y, const QString &z);

    // ---- look ----
    static QStringList colormapNames();
    static QStringList surfaceStyleNames();
    void setColormap(int index);
    void setSurfaceStyle(int index);            // 0 filled, 1 filled + mesh, 2 mesh only
    void setShading(bool on);
    void setShowBox(bool on);                   // back walls, grid, axes and tick labels
    void setColorByZ(bool on);                  // colour a line / points by height
    void setLineColor(const QColor &c);
    void setLineWidth(double w);
    void setPointSize(double s);
    void setEqualAspect(bool on);               // box proportional to the data ranges
    void setBoxHeight(double h);                // height of the box (width = depth = 1) when not equal
    void setZLimits(bool hasMin, double zmin, bool hasMax, double zmax);   // optional fixed Z range

    // ---- view ----
    void setView(double azimuthDeg, double elevationDeg);
    void resetView();
    double azimuth() const { return m_az; }
    double elevation() const { return m_el; }

    // ---- output ----
    // PNG / JPG (2x resolution) or PDF, chosen by the file extension.
    bool saveImage(const QString &path, QString *error = nullptr) const;
    // A pgfplots document of the scene (\addplot3), with the same view, colour map and labels.
    Plot3DLatexResult toPgfplots(const Plot3DLatexOptions &opts) const;

signals:
    void viewChanged(double azimuthDeg, double elevationDeg);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    QSize sizeHint() const override { return QSize(640, 480); }

private:
    struct Vec3 { double x = 0, y = 0, z = 0; };
    struct Formula {
        enum Kind { None, Surface, Curve } kind = None;
        QString ex, ey, ez;                      // surface: ez = f(x, y); curve: x(t), y(t), z(t)
        double a0 = 0, a1 = 1, b0 = 0, b1 = 1;   // surface: x and y range; curve: t range in a0..a1
        int samples = 0;
    };
    Formula m_formula;

    void paintScene(QPainter &p, const QRectF &area) const;
    void rebuild();                              // normalised coordinates, ranges, cell data
    void updateRanges();
    QColor colormapColor(double t) const;        // t in [0, 1]

    // data (as given)
    int m_nx = 0, m_ny = 0;
    QVector<double> m_sx, m_sy, m_sz;            // surface grid axes and heights
    QVector<double> m_lx, m_ly, m_lz;            // polyline
    QVector<double> m_px, m_py, m_pz;            // points

    // derived by rebuild()
    double m_xlo = 0, m_xhi = 1, m_ylo = 0, m_yhi = 1, m_zlo = 0, m_zhi = 1;   // data ranges
    double m_dx = 1, m_dy = 1, m_dz = 1;         // box size
    QVector<Vec3> m_surfN, m_lineN, m_ptsN;      // normalised (box-centred) positions
    QVector<float> m_cellT, m_cellNx, m_cellNy, m_cellNz;   // per surface cell: colour position and facet normal

    // text
    QString m_title, m_xl = "x", m_yl = "y", m_zl = "z";

    // look
    int m_cmap = 0, m_surfStyle = 0;
    bool m_shading = true, m_showBox = true, m_colorByZ = false, m_equal = false;
    QColor m_lineColor = QColor("#1f77b4");
    double m_lineWidth = 1.8, m_pointSize = 5.0, m_boxHeight = 0.8;
    bool m_hasZMin = false, m_hasZMax = false;
    double m_zMinFixed = 0, m_zMaxFixed = 1;

    // view
    double m_az = -37.5, m_el = 30.0, m_zoom = 1.0;
    QPointF m_pan;                               // in pixels
    QPoint m_lastMouse;
};
