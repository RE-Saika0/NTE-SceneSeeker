#include "MapView.h"

#include "Coordinates.h"

#include <QDir>
#include <QEnterEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

MapView::MapView(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMinimumSize(160, 120);
    m_cache.setMaxCost(CACHE_LIMIT);
}

MapView::~MapView() = default;

void MapView::setTileDirectory(const QString &dir)
{
    m_tileDir = dir;
    m_cache.clear();
    m_pending.clear();
    m_failed.clear();
    m_hasTiles = QFileInfo::exists(QDir(dir).filePath(QStringLiteral("6/0_0.jpg")))
        || QFileInfo::exists(QDir(dir).filePath(QStringLiteral("0/0_0.jpg")));
    update();
}

double MapView::fitScale() const
{
    if (width() <= 0 || height() <= 0)
        return 0.02;
    return std::min(width() / double(WORLD), height() / double(WORLD));
}

void MapView::clampCenter()
{
    if (m_scale <= 0.0)
        return;
    const double halfX = width() / (2.0 * m_scale);
    const double halfY = height() / (2.0 * m_scale);

    if (halfX * 2.0 >= WORLD)
        m_center.setX(WORLD / 2.0);
    else
        m_center.setX(std::clamp(m_center.x(), halfX, WORLD - halfX));

    if (halfY * 2.0 >= WORLD)
        m_center.setY(WORLD / 2.0);
    else
        m_center.setY(std::clamp(m_center.y(), halfY, WORLD - halfY));
}

QPointF MapView::worldToView(const QPointF &world) const
{
    return QPointF(width() * 0.5, height() * 0.5) + (world - m_center) * m_scale;
}

QPointF MapView::viewToWorld(const QPointF &view) const
{
    if (m_scale <= 0.0)
        return m_center;
    return m_center + (view - QPointF(width() * 0.5, height() * 0.5)) / m_scale;
}

QPointF MapView::normalizedToWorld(const QPointF &normalized) const
{
    return QPointF(normalized.x() * WORLD, normalized.y() * WORLD);
}

QPointF MapView::viewToNormalized(const QPointF &view) const
{
    const QPointF world = viewToWorld(view);
    return QPointF(std::clamp(world.x() / WORLD, 0.0, 1.0),
                   std::clamp(world.y() / WORLD, 0.0, 1.0));
}

void MapView::resetView()
{
    m_scale = fitScale();
    if (m_scale <= 0.0)
        m_scale = 0.02;
    m_center = QPointF(WORLD / 2.0, WORLD / 2.0);
    m_fitMode = true;
    clampCenter();
    update();
}

void MapView::setViewState(const QPointF &center, double scale, bool fitMode)
{
    m_center = center;
    m_scale = scale;
    m_fitMode = fitMode;
    if (m_fitMode || m_scale <= 0.0) {
        resetView();
        return;
    }
    m_scale = std::clamp(m_scale, fitScale(), 1.0);
    clampCenter();
    update();
}

void MapView::focusOn(const QPointF &normalized, double scale)
{
    const QPointF n = Coordinates::clampNormalized(normalized);
    m_fitMode = false;
    double targetScale = scale;
    if (targetScale <= 0.0)
        targetScale = std::max(fitScale() * 3.5, 0.07);
    m_scale = std::clamp(targetScale, fitScale(), 1.0);
    m_center = normalizedToWorld(n);
    clampCenter();
    update();
}

void MapView::fitToPoints(const QPointF &a, const QPointF &b)
{
    QVector<QPointF> points{a, b};
    fitToPointsPadded(points, 0.10);
}

bool MapView::fitTargetForPoints(const QVector<QPointF> &points, qreal padding,
                                 QPointF *center, double *scale, qreal topInsetPx) const
{
    if (points.isEmpty() || width() <= 0 || height() <= 0)
        return false;

    qreal minX = 1.0, maxX = 0.0, minY = 1.0, maxY = 0.0;
    for (const QPointF &raw : points) {
        const QPointF point = Coordinates::clampNormalized(raw);
        minX = std::min(minX, point.x());
        maxX = std::max(maxX, point.x());
        minY = std::min(minY, point.y());
        maxY = std::max(maxY, point.y());
    }

    // 单点或几乎重合时给一个最小跨度，避免无限放大。
    const qreal minSpan = 0.045;
    const qreal dx = std::max(maxX - minX, minSpan);
    const qreal dy = std::max(maxY - minY, minSpan);

    // 顶部被 HUD/分数框占掉的高度：可用区域从 topInset 往下算。
    const double topInset = qBound(0.0, double(topInsetPx), height() * 0.6);
    const double usableHeight = qMax(1.0, height() - topInset);

    // 横向留白按整宽算；纵向留白只按"未被遮挡的那部分高度"算。
    const qreal usableX = qBound<qreal>(0.3, 1.0 - 2.0 * padding, 1.0);
    const qreal usableY = qBound<qreal>(0.3, 1.0 - 2.0 * padding, 1.0);

    const double sx = width() * usableX / (dx * WORLD);
    const double sy = usableHeight * usableY / (dy * WORLD);
    const double finalScale = std::clamp(std::min(sx, sy), fitScale(), 0.6);

    if (center) {
        const double cx = (minX + maxX) * 0.5 * WORLD;
        const double cy = (minY + maxY) * 0.5 * WORLD;
        // 视野中心随之下移，使点群的视觉中心落在 topInset 以下的可用区域中。
        const double targetScreenY = topInset + usableHeight * 0.5;
        const double shiftWorldY = (targetScreenY - height() * 0.5) / finalScale;
        *center = QPointF(cx, cy - shiftWorldY);
    }
    if (scale)
        *scale = finalScale;
    return true;
}

void MapView::fitToPointsPadded(const QVector<QPointF> &points, qreal padding)
{
    QPointF center;
    double scale = 0.0;
    if (!fitTargetForPoints(points, padding, &center, &scale))
        return;
    m_scale = scale;
    m_center = center;
    m_fitMode = false;
    clampCenter();
    update();
}

void MapView::setPin(const QPointF &normalized, bool visible)
{
    m_pin = Coordinates::clampNormalized(normalized);
    m_pinVisible = visible;
    update();
}

void MapView::clearPin()
{
    m_pinVisible = false;
    update();
}

void MapView::setPinEditable(bool editable)
{
    m_pinEditable = editable;
    update();
}

void MapView::setTruthPoint(const QPointF &normalized, bool visible)
{
    m_truth = Coordinates::clampNormalized(normalized);
    m_truthVisible = visible;
    update();
}

void MapView::clearTruthPoint()
{
    m_truthVisible = false;
    update();
}

void MapView::setShowConnection(bool show)
{
    m_showConnection = show;
    update();
}

void MapView::setLabels(const QVector<Label> &labels)
{
    m_labels = labels;
    update();
}

void MapView::setOtherPins(const QVector<QPointF> &points)
{
    m_otherPins.clear();
    m_otherPins.reserve(points.size());
    for (const QPointF &point : points)
        m_otherPins.append(Coordinates::clampNormalized(point));
    update();
}

void MapView::clearOtherPins()
{
    m_otherPins.clear();
    m_otherPinColors.clear();
    m_otherDistanceLabels.clear();
    update();
}

void MapView::setOtherPinColors(const QVector<QColor> &colors)
{
    m_otherPinColors = colors;
    update();
}

void MapView::setPinColor(const QColor &color)
{
    m_pinColor = color;
    update();
}

void MapView::setDistanceLabels(const QString &localLabel, const QVector<QString> &otherLabels)
{
    m_localDistanceLabel = localLabel;
    m_otherDistanceLabels = otherLabels;
    update();
}

void MapView::setOtherPinsReveal(qreal value)
{
    m_otherPinsReveal = qBound<qreal>(0.0, value, 1.0);
    update();
}

void MapView::setTruthReveal(qreal value)
{
    m_truthReveal = qBound<qreal>(0.0, value, 1.6);
    update();
}

void MapView::setConnectionReveal(qreal value)
{
    m_connectionReveal = qBound<qreal>(0.0, value, 1.0);
    update();
}

void MapView::setDistanceLabelsReveal(qreal value)
{
    m_distanceLabelsReveal = qBound<qreal>(0.0, value, 1.0);
    update();
}

int MapView::chooseLevel(double scale) const
{
    // 粗到细：第一个图像宽度 >= 当前所需屏幕像素宽度的层级。
    const double needed = WORLD * scale * devicePixelRatioF();
    for (int level = 6; level >= 0; --level) {
        if (double(COLS[level]) * TILE >= needed)
            return level;
    }
    return 0;
}

int MapView::currentLevel() const
{
    if (m_scale <= 0.0)
        return 6;
    return chooseLevel(m_scale);
}

QString MapView::tilePath(int level, int x, int y) const
{
    return QDir(m_tileDir).filePath(QStringLiteral("%1/%2_%3.jpg").arg(level).arg(x).arg(y));
}

QPixmap *MapView::cachedTile(const TileKey &key)
{
    QPixmap *pixmap = m_cache.object(key);
    if (!pixmap)
        return nullptr;
    // QCache::object() 不更新 LRU，take + insert 相当于 touch。
    QPixmap *held = m_cache.take(key);
    if (!held)
        return nullptr;
    m_cache.insert(key, held, 1);
    return held;
}

void MapView::requestTile(const TileKey &key)
{
    if (m_tileDir.isEmpty() || m_cache.contains(key) || m_pending.contains(key) || m_failed.contains(key))
        return;
    // 限制同时解码数量，拖动时不会创建过多短任务。
    if (m_pending.size() >= 64)
        return;

    const QString path = tilePath(key.level, key.x, key.y);
    if (!QFileInfo::exists(path)) {
        m_failed.insert(key);
        return;
    }

    m_pending.insert(key);
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, key]() {
        const QImage image = watcher->result();
        watcher->deleteLater();
        m_pending.remove(key);
        if (image.isNull()) {
            m_failed.insert(key);
            return;
        }
        const QPixmap pixmap = QPixmap::fromImage(image);
        if (pixmap.isNull()) {
            m_failed.insert(key);
            return;
        }
        m_cache.insert(key, new QPixmap(pixmap), 1);
        update();
    });

    watcher->setFuture(QtConcurrent::run([path]() {
        QImageReader reader(path);
        reader.setAutoTransform(false);
        return reader.read();
    }));
}

void MapView::requestLevel(int level, const QRectF &worldRegion)
{
    const int cols = COLS[level];
    const double tileWorld = double(WORLD) / cols;
    const int x0 = std::clamp(int(std::floor(worldRegion.left() / tileWorld)), 0, cols - 1);
    const int x1 = std::clamp(int(std::floor((worldRegion.right() - 1e-6) / tileWorld)), 0, cols - 1);
    const int y0 = std::clamp(int(std::floor(worldRegion.top() / tileWorld)), 0, cols - 1);
    const int y1 = std::clamp(int(std::floor((worldRegion.bottom() - 1e-6) / tileWorld)), 0, cols - 1);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x)
            requestTile(TileKey{level, x, y});
    }
}

void MapView::drawLevel(QPainter &painter, int level, const QRectF &worldRegion, bool fillMissing)
{
    const int cols = COLS[level];
    const double tileWorld = double(WORLD) / cols;
    const int x0 = std::clamp(int(std::floor(worldRegion.left() / tileWorld)), 0, cols - 1);
    const int x1 = std::clamp(int(std::floor((worldRegion.right() - 1e-6) / tileWorld)), 0, cols - 1);
    const int y0 = std::clamp(int(std::floor(worldRegion.top() / tileWorld)), 0, cols - 1);
    const int y1 = std::clamp(int(std::floor((worldRegion.bottom() - 1e-6) / tileWorld)), 0, cols - 1);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const TileKey key{level, x, y};
            const QRectF target(worldToView(QPointF(x * tileWorld, y * tileWorld)),
                                worldToView(QPointF((x + 1) * tileWorld, (y + 1) * tileWorld)));
            QPixmap *pixmap = cachedTile(key);
            if (!pixmap) {
                requestTile(key);
                if (fillMissing) {
                    painter.fillRect(target, QColor(QStringLiteral("#0b0d10")));
                    painter.setPen(QPen(QColor(255, 255, 255, 5), 1.0));
                    painter.drawRect(target.adjusted(0.5, 0.5, -0.5, -0.5));
                }
                continue;
            }

            // 极小的外扩用于盖住浮点边界上的发丝接缝。
            const QRectF drawRect = target.adjusted(-0.35, -0.35, 0.35, 0.35);
            painter.drawPixmap(drawRect, *pixmap,
                               QRectF(0, 0, pixmap->width(), pixmap->height()));
        }
    }
}

void MapView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), QColor(QStringLiteral("#05070a")));

    if (m_scale <= 0.0)
        m_scale = fitScale();

    const QPointF topLeft = viewToWorld(QPointF(0, 0));
    const QPointF bottomRight = viewToWorld(QPointF(width(), height()));
    const QRectF visibleWorld = QRectF(topLeft, bottomRight).normalized()
        & QRectF(0, 0, WORLD, WORLD);

    if (m_hasTiles && !visibleWorld.isEmpty()) {
        const int level = chooseLevel(m_scale);
        // 粗一级作为底图：当前层没加载完时不会白屏。
        if (level < 6)
            drawLevel(painter, level + 1, visibleWorld, true);
        drawLevel(painter, level, visibleWorld, false);

        const double tileWorld = double(WORLD) / COLS[level];
        requestLevel(level, visibleWorld.adjusted(-tileWorld, -tileWorld, tileWorld, tileWorld));
        if (level < 6) {
            const double baseTileWorld = double(WORLD) / COLS[level + 1];
            requestLevel(level + 1,
                         visibleWorld.adjusted(-baseTileWorld, -baseTileWorld,
                                               baseTileWorld, baseTileWorld));
        }
    } else {
        painter.setPen(QColor(125, 155, 175));
        QFont font = painter.font();
        font.setPointSize(11);
        painter.setFont(font);
        painter.drawText(rect().adjusted(24, 24, -24, -24),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         m_tileDir.isEmpty()
                             ? QStringLiteral("未设置地图瓦片目录")
                             : QStringLiteral("未找到地图瓦片\nassets/map/tiles/{level}/{x}_{y}.jpg"));
    }

    drawMarkers(painter);
    drawLabels(painter);
}

void MapView::drawLabels(QPainter &painter)
{
    if (m_labels.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF visible = rect().adjusted(-120, -120, 120, 120);
    for (const Label &label : m_labels) {
        if (label.text.isEmpty())
            continue;
        const QPointF pos = worldToView(normalizedToWorld(label.pos));
        if (!visible.contains(pos))
            continue;

        painter.save();
        painter.translate(pos);
        painter.rotate(label.angle);

        QFont font = painter.font();
        font.setPixelSize(qMax(10, label.size));
        font.setWeight(QFont::DemiBold);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
        painter.setFont(font);

        QFontMetricsF metrics(font);
        const qreal width = metrics.horizontalAdvance(label.text);
        QPainterPath path;
        path.addText(-width / 2.0, metrics.ascent() / 2.0 - metrics.descent() / 2.0,
                     font, label.text);
        painter.setPen(QPen(QColor(6, 12, 18, 200), 3.6, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(QColor(234, 243, 249, 235));
        painter.drawPath(path);
        painter.restore();
    }
    painter.restore();
}

void MapView::drawMarkers(QPainter &painter)
{
    const QPointF pinPos = worldToView(normalizedToWorld(m_pin));
    const QPointF truthPos = worldToView(normalizedToWorld(m_truth));

    if (m_showConnection && m_pinVisible && m_truthVisible && m_connectionReveal > 0.0) {
        QPen pen(QColor(220, 238, 248, 175), 1.5, Qt::DashLine);
        pen.setDashPattern({5.0, 4.0});
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(pinPos, pinPos + (truthPos - pinPos) * m_connectionReveal);
    }

    if (m_pinVisible) {
        QColor halo = m_pinColor;
        halo.setAlpha(45);
        painter.setPen(Qt::NoPen);
        painter.setBrush(halo);
        painter.drawEllipse(pinPos, 13.0, 13.0);
        painter.setPen(QPen(QColor(255, 255, 255, 235), 2.0));
        painter.setBrush(m_pinColor);
        painter.drawEllipse(pinPos, 7.5, 7.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#07141b")));
        painter.drawEllipse(pinPos, 2.5, 2.5);
    }

    // 其他玩家的标点：每个用他自己的主题色（和顶部血条头像同色）。
    painter.save();
    painter.setOpacity(m_otherPinsReveal);
    const qreal otherRadius = 2.0 + 4.0 * m_otherPinsReveal;
    for (int i = 0; i < m_otherPins.size(); ++i) {
        const QPointF pos = worldToView(normalizedToWorld(m_otherPins.at(i)));
        const QColor color = (i < m_otherPinColors.size()) ? m_otherPinColors.at(i)
                                                          : QColor(QStringLiteral("#A78BFA"));
        painter.setPen(QPen(QColor(255, 255, 255, 220), 1.5));
        painter.setBrush(color);
        painter.drawEllipse(pos, otherRadius, otherRadius);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color.darker(280));
        painter.drawEllipse(pos, 2.0, 2.0);
    }
    painter.restore();

    if (m_truthVisible) {
        painter.save();
        painter.translate(truthPos);
        painter.setOpacity(qMin<qreal>(1.0, m_truthReveal));
        const qreal truthScale = 0.20 + 0.80 * m_truthReveal;
        painter.scale(truthScale, truthScale);
        QPainterPath diamond;
        diamond.moveTo(0, -10);
        diamond.lineTo(9, 0);
        diamond.lineTo(0, 10);
        diamond.lineTo(-9, 0);
        diamond.closeSubpath();
        painter.setPen(QPen(QColor(255, 255, 255, 240), 2.0));
        painter.setBrush(QColor(QStringLiteral("#ffb454")));
        painter.drawPath(diamond);
        painter.restore();
    }

    if (m_distanceLabelsReveal > 0.0) {
        painter.save();
        painter.setOpacity(m_distanceLabelsReveal);
        QFont labelFont = painter.font();
        labelFont.setPointSize(9);
        labelFont.setWeight(QFont::Bold);
        painter.setFont(labelFont);
        auto drawLabel = [&painter](const QPointF &pos, const QString &text, const QColor &edge) {
            if (text.isEmpty())
                return;
            const QRect textRect = painter.fontMetrics().boundingRect(text).adjusted(-7, -4, 7, 4);
            QRect box = textRect;
            box.moveBottomLeft(QPoint(qRound(pos.x() - box.width() / 2.0), qRound(pos.y() - 12)));
            painter.setPen(QPen(edge, 1.0));
            painter.setBrush(QColor(8, 15, 27, 220));
            painter.drawRoundedRect(box, 5, 5);
            painter.setPen(Qt::white);
            painter.drawText(box, Qt::AlignCenter, text);
        };
        if (m_pinVisible)
            drawLabel(pinPos, m_localDistanceLabel, m_pinColor);
        for (int i = 0; i < m_otherPins.size(); ++i)
            drawLabel(worldToView(normalizedToWorld(m_otherPins.at(i))),
                      i < m_otherDistanceLabels.size() ? m_otherDistanceLabels.at(i) : QString(),
                      (i < m_otherPinColors.size()) ? m_otherPinColors.at(i)
                                                    : QColor(167, 139, 250));
        painter.restore();
    }
}

void MapView::setResizeSuspended(bool suspended)
{
    m_resizeSuspended = suspended;
}

void MapView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_resizeSuspended) {
        update();
        return;
    }
    if (m_fitMode || m_scale <= 0.0)
        resetView();
    else {
        m_scale = std::clamp(m_scale, fitScale(), 1.0);
        clampCenter();
        update();
    }
}

void MapView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    setFocus(Qt::MouseFocusReason);
    m_leftDown = true;
    m_moved = false;
    m_pressPos = event->pos();
    m_lastMouse = event->pos();

    if (m_pinEditable && m_pinVisible) {
        const QPointF pinPos = worldToView(normalizedToWorld(m_pin));
        if (QLineF(pinPos, event->position()).length() <= 16.0) {
            m_draggingPin = true;
            m_dragging = false;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }

    m_dragging = true;
    m_draggingPin = false;
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void MapView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_draggingPin) {
        const QPointF normalized = viewToNormalized(event->position());
        m_pin = normalized;
        emit pinChanged(normalized);
        update();
        event->accept();
        return;
    }

    if (m_dragging && m_leftDown) {
        const QPoint delta = event->pos() - m_lastMouse;
        if (delta.manhattanLength() > 2)
            m_moved = true;
        m_lastMouse = event->pos();
        m_center -= QPointF(delta.x(), delta.y()) / m_scale;
        m_fitMode = false;
        clampCenter();
        update();
        event->accept();
        return;
    }

    updateHoverCursor(event->position());
    QWidget::mouseMoveEvent(event);
}

void MapView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    if (m_draggingPin) {
        const QPointF normalized = viewToNormalized(event->position());
        m_pin = normalized;
        emit pinChanged(normalized);
    } else if (m_dragging && !m_moved && m_pinEditable) {
        // 只读（已提交答案）时点击不再改标点。
        emit mapClicked(viewToNormalized(event->position()));
    }

    m_leftDown = false;
    m_dragging = false;
    m_draggingPin = false;
    updateHoverCursor(event->position());
    update();
    event->accept();
}

void MapView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        resetView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MapView::wheelEvent(QWheelEvent *event)
{
    if (event->angleDelta().y() == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    const double oldScale = m_scale > 0.0 ? m_scale : fitScale();
    const QPointF anchorWorld = viewToWorld(event->position());
    const double factor = std::pow(1.0015, double(event->angleDelta().y()));
    const double newScale = std::clamp(oldScale * factor, fitScale(), 1.0);
    if (qFuzzyCompare(newScale, oldScale)) {
        event->accept();
        return;
    }

    m_scale = newScale;
    m_center = anchorWorld
        - (event->position() - QPointF(width() * 0.5, height() * 0.5)) / m_scale;
    m_fitMode = false;
    clampCenter();
    update();
    event->accept();
}

void MapView::enterEvent(QEnterEvent *event)
{
    updateHoverCursor(event->position());
    QWidget::enterEvent(event);
}

void MapView::updateHoverCursor(const QPointF &pos)
{
    if (m_dragging || m_draggingPin) {
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_pinEditable && m_pinVisible) {
        const QPointF pinPos = worldToView(normalizedToWorld(m_pin));
        if (QLineF(pinPos, pos).length() <= 16.0) {
            setCursor(Qt::PointingHandCursor);
            return;
        }
    }
    setCursor(Qt::OpenHandCursor);
}
