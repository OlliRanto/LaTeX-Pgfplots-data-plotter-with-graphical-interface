#include "DraggableVLine.h"
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

DraggableVLineGroup::DraggableVLineGroup(QCustomPlot *plot, const QVector<double> &xs,
                                          const QVector<QColor> &colors,
                                          std::function<void()> onChange,
                                          std::function<QVector<double>()> snapProvider)
    : m_plot(plot), m_onChange(std::move(onChange)), m_snapProvider(std::move(snapProvider)) {
    double yLower = plot->yAxis->range().lower;
    double yUpper = plot->yAxis->range().upper;
    for (int i = 0; i < xs.size(); ++i) {
        auto *line = new QCPItemStraightLine(plot);
        line->point1->setCoords(xs[i], yLower);
        line->point2->setCoords(xs[i], yUpper);
        QColor c = colors.isEmpty() ? QColor("black") : colors[i % colors.size()];
        QPen pen(c);
        pen.setStyle(Qt::DashLine);
        pen.setWidth(2);
        line->setPen(pen);
        line->setSelectable(false);
        m_lines.push_back(line);
    }
    plot->replot();
    plot->installEventFilter(this);
}

DraggableVLineGroup::~DraggableVLineGroup() { remove(); }

void DraggableVLineGroup::remove() {
    if (m_removed) return;
    m_removed = true;
    if (m_plot) m_plot->removeEventFilter(this);
    for (auto *line : m_lines) {
        if (m_plot) m_plot->removeItem(line);
    }
    m_lines.clear();
    if (m_plot) m_plot->replot();
}

QVector<double> DraggableVLineGroup::allX() const {
    QVector<double> out;
    for (auto *line : m_lines) out << line->point1->coords().x();
    return out;
}

void DraggableVLineGroup::setAllX(const QVector<double> &xs) {
    for (int i = 0; i < m_lines.size() && i < xs.size(); ++i) {
        double y1 = m_lines[i]->point1->coords().y();
        double y2 = m_lines[i]->point2->coords().y();
        m_lines[i]->point1->setCoords(xs[i], y1);
        m_lines[i]->point2->setCoords(xs[i], y2);
    }
    if (m_plot) m_plot->replot();
    if (m_onChange) m_onChange();
}

QPair<double, double> DraggableVLineGroup::bounds() const {
    QVector<double> xs = allX();
    if (xs.isEmpty()) return { 0.0, 0.0 };
    auto mm = std::minmax_element(xs.begin(), xs.end());
    return { *mm.first, *mm.second };
}

double DraggableVLineGroup::nearestPixelLineIndex(const QPoint &pos, double maxPixelDist) const {
    int best = -1;
    double bestDist = maxPixelDist;
    for (int i = 0; i < m_lines.size(); ++i) {
        double xPix = m_plot->xAxis->coordToPixel(m_lines[i]->point1->coords().x());
        double d = std::fabs(pos.x() - xPix);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

double DraggableVLineGroup::snapIfNeeded(double x) const {
    if (!m_snapProvider) return x;
    QVector<double> snapVals = m_snapProvider();
    if (snapVals.isEmpty()) return x;
    double best = snapVals.first();
    double bestDist = std::fabs(x - best);
    for (double v : snapVals) {
        double d = std::fabs(x - v);
        if (d < bestDist) { bestDist = d; best = v; }
    }
    return best;
}

bool DraggableVLineGroup::eventFilter(QObject *watched, QEvent *event) {
    if (m_removed || watched != m_plot) return false;

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton) return false;
        int idx = (int)nearestPixelLineIndex(me->pos(), 8.0);
        if (idx >= 0) {
            m_dragIndex = idx;
            return true; // swallow: prevent QCP's own range-drag from starting
        }
        return false;
    }
    if (event->type() == QEvent::MouseMove) {
        if (m_dragIndex < 0) return false;
        auto *me = static_cast<QMouseEvent *>(event);
        double x = m_plot->xAxis->pixelToCoord(me->pos().x());
        x = snapIfNeeded(x);
        double y1 = m_lines[m_dragIndex]->point1->coords().y();
        double y2 = m_lines[m_dragIndex]->point2->coords().y();
        m_lines[m_dragIndex]->point1->setCoords(x, y1);
        m_lines[m_dragIndex]->point2->setCoords(x, y2);
        m_plot->replot(QCustomPlot::rpQueuedReplot);
        if (m_onChange) m_onChange();
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        if (m_dragIndex < 0) return false;
        m_dragIndex = -1;
        if (m_onChange) m_onChange();
        return true;
    }
    return false;
}
