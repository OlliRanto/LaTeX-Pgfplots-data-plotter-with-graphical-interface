#pragma once
#include "qcustomplot.h"
#include <functional>
#include <QVector>
#include <QColor>

// One or more draggable vertical lines on a QCustomPlot, used for the
// fit-range selector, integral-range selector, and interpolation cursor --
// mirrors DraggableVLineGroup from the Python/matplotlib app.
//
// Click-and-drag a line with the mouse to move it (an internal event filter
// intercepts clicks near a line so QCustomPlot's own axis-rect panning
// doesn't fire instead). Optionally snaps to the nearest value from a
// caller-supplied array while dragging.
class DraggableVLineGroup : public QObject {
    Q_OBJECT
public:
    // xs: initial x positions (one QCPItemStraightLine per entry).
    // colors: parallel color list (reused/cycled if shorter than xs).
    // onChange: called continuously while dragging and once on release.
    // snapProvider: optional; return a sorted list of "snap-able" x values,
    // or an empty list to disable snapping.
    DraggableVLineGroup(QCustomPlot *plot, const QVector<double> &xs,
                         const QVector<QColor> &colors,
                         std::function<void()> onChange,
                         std::function<QVector<double>()> snapProvider = nullptr);
    ~DraggableVLineGroup() override;

    QVector<double> allX() const;
    void setAllX(const QVector<double> &xs);

    // sorted (min, max) across all lines -- handy for 2-line fit/integral ranges
    QPair<double, double> bounds() const;

    // Detach the lines from the plot and delete this object's QCP items.
    // Safe to call once; further calls are no-ops.
    void remove();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    double nearestPixelLineIndex(const QPoint &pos, double maxPixelDist) const;
    double snapIfNeeded(double x) const;

    QCustomPlot *m_plot;
    QVector<QCPItemStraightLine *> m_lines;
    std::function<void()> m_onChange;
    std::function<QVector<double>()> m_snapProvider;
    int m_dragIndex = -1;
    bool m_removed = false;
};
