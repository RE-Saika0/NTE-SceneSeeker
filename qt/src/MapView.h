#pragma once

#include <QCache>
#include <QHashFunctions>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

class QPainter;

// 基于 7 级瓦片金字塔的自绘地图控件。
// 负责平移、以鼠标为锚点缩放、双击复位、落点与 pin 拖动，所有坐标以归一化值对外。
class MapView : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal otherPinsReveal READ otherPinsReveal WRITE setOtherPinsReveal)
    Q_PROPERTY(qreal truthReveal READ truthReveal WRITE setTruthReveal)
    Q_PROPERTY(qreal connectionReveal READ connectionReveal WRITE setConnectionReveal)
    Q_PROPERTY(qreal distanceLabelsReveal READ distanceLabelsReveal WRITE setDistanceLabelsReveal)

public:
    static constexpr int WORLD = 26112;
    static constexpr int TILE = 512;
    static constexpr int COLS[7] = {51, 26, 13, 7, 4, 2, 1};
    static constexpr int CACHE_LIMIT = 200;

    struct TileKey {
        int level = 0;
        int x = 0;
        int y = 0;

        bool operator==(const TileKey &other) const
        {
            return level == other.level && x == other.x && y == other.y;
        }
    };

    explicit MapView(QWidget *parent = nullptr);
    ~MapView() override;

    void setTileDirectory(const QString &dir);
    bool hasTiles() const { return m_hasTiles; }

    // 全图 fit（双击复位走这里）。
    void resetView();
    // 以某个归一化坐标为中心放大；scale <= 0 时使用默认倍率。
    void focusOn(const QPointF &normalized, double scale = -1.0);
    // 让两个归一化点都进入视野（用于结果回顾）。
    void fitToPoints(const QPointF &a, const QPointF &b);
    // 让一组点全部落入视口（padding 为四周留白比例，0.1 = 四周各留 10%）。
    // topInsetPx：顶部被 HUD/分数框占掉的像素高度，点不会落进这块区域（不改地图布局）。
    void fitToPointsPadded(const QVector<QPointF> &points, qreal padding = 0.1);
    bool fitTargetForPoints(const QVector<QPointF> &points, qreal padding,
                            QPointF *center, double *scale,
                            qreal topInsetPx = 0.0) const;

    // 路名/地名标签：归一化坐标 + 旋转角（度）+ 字号。
    struct Label {
        QString text;
        QPointF pos{0.5, 0.5};
        qreal angle = 0.0;
        int size = 24;
    };
    void setLabels(const QVector<Label> &labels);
    void setPin(const QPointF &normalized, bool visible = true);
    void clearPin();
    QPointF pin() const { return m_pin; }
    bool hasPin() const { return m_pinVisible; }
    void setPinEditable(bool editable);
    void setTruthPoint(const QPointF &normalized, bool visible = true);
    void clearTruthPoint();
    void setShowConnection(bool show);
    // 打开/关闭动画期间冻结视图：否则每帧 resize 都会 clamp，导致视角漂移。
    void setResizeSuspended(bool suspended);
    // Read-only markers for multiplayer results. The editable/local pin remains
    // separate so all remote guesses can be shown at once.
    void setOtherPins(const QVector<QPointF> &points);
    // 每个玩家自己的主题色（和顶部血条头像同色）。索引与 setOtherPins 对齐。
    void setOtherPinColors(const QVector<QColor> &colors);
    // 自己的 pin 的颜色。
    void setPinColor(const QColor &color);
    void clearOtherPins();
    void setDistanceLabels(const QString &localLabel, const QVector<QString> &otherLabels);
    qreal otherPinsReveal() const { return m_otherPinsReveal; }
    qreal truthReveal() const { return m_truthReveal; }
    qreal connectionReveal() const { return m_connectionReveal; }
    qreal distanceLabelsReveal() const { return m_distanceLabelsReveal; }
    void setOtherPinsReveal(qreal value);
    void setTruthReveal(qreal value);
    void setConnectionReveal(qreal value);
    void setDistanceLabelsReveal(qreal value);

    double currentScale() const { return m_scale; }
    int currentLevel() const;
    QPointF viewCenter() const { return m_center; }
    bool isFitMode() const { return m_fitMode; }
    void setViewState(const QPointF &center, double scale, bool fitMode);

signals:
    void mapClicked(QPointF normalized);
    void pinChanged(QPointF normalized);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void enterEvent(QEnterEvent *event) override;

private:
    double fitScale() const;
    void clampCenter();
    QPointF worldToView(const QPointF &world) const;
    QPointF viewToWorld(const QPointF &view) const;
    QPointF normalizedToWorld(const QPointF &normalized) const;
    QPointF viewToNormalized(const QPointF &view) const;
    int chooseLevel(double scale) const;

    QString tilePath(int level, int x, int y) const;
    QPixmap *cachedTile(const TileKey &key);
    void requestTile(const TileKey &key);
    void requestLevel(int level, const QRectF &worldRegion);
    void drawLevel(QPainter &painter, int level, const QRectF &worldRegion, bool fillMissing);
    void drawMarkers(QPainter &painter);
    void drawLabels(QPainter &painter);
    void updateHoverCursor(const QPointF &pos);

    QString m_tileDir;
    bool m_hasTiles = false;

    double m_scale = 0.0;                 // view px / world px
    QPointF m_center{WORLD / 2.0, WORLD / 2.0};
    bool m_fitMode = true;

    QPointF m_pin{0.5, 0.5};
    bool m_pinVisible = false;
    bool m_pinEditable = true;
    QPointF m_truth{0.5, 0.5};
    bool m_truthVisible = false;
    bool m_showConnection = false;
    QVector<QPointF> m_otherPins;
    QVector<QColor> m_otherPinColors;
    QColor m_pinColor{105, 224, 255};
    QVector<Label> m_labels;
    QString m_localDistanceLabel;
    QVector<QString> m_otherDistanceLabels;
    qreal m_otherPinsReveal = 1.0;
    qreal m_truthReveal = 1.0;
    qreal m_connectionReveal = 1.0;
    qreal m_distanceLabelsReveal = 0.0;

    bool m_leftDown = false;
    bool m_dragging = false;
    bool m_draggingPin = false;
    bool m_moved = false;
    bool m_resizeSuspended = false;
    QPoint m_pressPos;
    QPoint m_lastMouse;

    QCache<TileKey, QPixmap> m_cache;
    QSet<TileKey> m_pending;
    QSet<TileKey> m_failed;
};

inline size_t qHash(const MapView::TileKey &key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.level, key.x, key.y);
}
