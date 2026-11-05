#include "GameWindow.h"

#include "AssetPaths.h"
#include "BackgroundCarousel.h"
#include "BgmPlayer.h"
#include "Coordinates.h"
#include "CoverTitleWidget.h"
#include "MapOverlay.h"
#include "MultiplayerSession.h"
#include "PortableSettings.h"
#include "bank/QuestionBank.h"
#include "Score.h"
#include "SoundPlayer.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QTextEdit>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QString formatClock(int seconds)
{
    const int safe = std::max(0, seconds);
    return QStringLiteral("%1:%2")
        .arg(safe / 60, 2, 10, QLatin1Char('0'))
        .arg(safe % 60, 2, 10, QLatin1Char('0'));
}

bool readNumber(const QJsonObject &object, const QString &key, double *value)
{
    const QJsonValue v = object.value(key);
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    if (!std::isfinite(d))
        return false;
    *value = d;
    return true;
}

} // namespace

// 题目截图显示：等比缩放，深色留边，不裁切，支持滚轮缩放和拖动。
class ScreenshotView : public QWidget
{
public:
    explicit ScreenshotView(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setMouseTracking(true);
    }

    void setSource(const QPixmap &source)
    {
        m_source = source;
        m_scaled = QPixmap();
        m_zoom = 1.0;
        m_offset = QPointF(0, 0);
        updateScaled();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.fillRect(rect(), QColor(QStringLiteral("#0B0E14")));

        if (m_source.isNull()) {
            painter.setPen(QColor(QStringLiteral("#8ba0ad")));
            QFont font = painter.font();
            font.setPointSize(13);
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("未找到题目截图"));
            return;
        }

        if (!m_scaled.isNull()) {
            const QSizeF zoomedSize(m_targetSize.width() * m_zoom, m_targetSize.height() * m_zoom);
            const QPointF baseTopLeft((width() - m_targetSize.width()) / 2.0,
                                      (height() - m_targetSize.height()) / 2.0);
            const QPointF topLeft = baseTopLeft + m_offset;
            
            // Always transform from the original pixmap.  Drawing the already
            // viewport-scaled cache here would enlarge it a second time and
            // make zoomed-in screenshots visibly soft.
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            painter.drawPixmap(QRectF(topLeft, zoomedSize), m_source, m_source.rect());
            
            painter.setPen(QPen(QColor(255, 255, 255, 15), 1));
            painter.drawRect(QRectF(topLeft, zoomedSize).adjusted(0, 0, -1, -1));
        }
        
        if (m_zoom != 1.0 || m_offset != QPointF(0, 0)) {
            QFont hintFont = painter.font();
            hintFont.setPointSize(10);
            painter.setFont(hintFont);
            painter.setPen(QColor(QStringLiteral("#69e0ff")));
            const QString hint = QStringLiteral("滚轮缩放 · 拖动查看 · 双击重置");
            painter.drawText(rect().adjusted(14, 0, -14, -12), Qt::AlignBottom | Qt::AlignRight, hint);
        }
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateScaled();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (m_source.isNull())
            return;
        
        const double steps = event->angleDelta().y() / 120.0;
        if (qFuzzyIsNull(steps))
            return;

        // Preserve the exact pixel under the pointer while zooming.
        const double oldZoom = m_zoom;
        m_zoom = qBound(1.0, oldZoom * std::pow(1.15, steps), 4.0);
        const QSizeF baseSize(m_targetSize);
        const QPointF baseTopLeft((width() - baseSize.width()) / 2.0,
                                  (height() - baseSize.height()) / 2.0);
        const QPointF oldTopLeft = baseTopLeft + m_offset;
        const QPointF sourcePoint((event->position().x() - oldTopLeft.x()) / (baseSize.width() * oldZoom),
                                  (event->position().y() - oldTopLeft.y()) / (baseSize.height() * oldZoom));
        const QPointF newTopLeft(event->position().x() - sourcePoint.x() * baseSize.width() * m_zoom,
                                 event->position().y() - sourcePoint.y() * baseSize.height() * m_zoom);
        m_offset = newTopLeft - baseTopLeft;
        constrainOffset();
        update();
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !m_source.isNull()) {
            m_dragging = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging) {
            const QPoint delta = event->pos() - m_lastMousePos;
            m_offset += delta;
            constrainOffset();
            m_lastMousePos = event->pos();
            update();
            event->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            setCursor(Qt::ArrowCursor);
            event->accept();
        }
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        Q_UNUSED(event)
        if (!m_source.isNull()) {
            m_zoom = 1.0;
            m_offset = QPointF(0, 0);
            update();
        }
    }

private:
    void updateScaled()
    {
        if (m_source.isNull() || width() <= 0 || height() <= 0) {
            m_scaled = QPixmap();
            return;
        }

        // At 100% the picture must already cover the entire view.  This avoids
        // exposing the dark background; any excess image area is cropped.
        m_targetSize = m_source.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
        if (m_targetSize.isEmpty()) {
            m_scaled = QPixmap();
            return;
        }

        const qreal dpr = devicePixelRatioF();
        const QSize pixelSize(qMax(1, qRound(m_targetSize.width() * dpr)),
                              qMax(1, qRound(m_targetSize.height() * dpr)));
        m_scaled = m_source.scaled(pixelSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_scaled.setDevicePixelRatio(dpr);
        constrainOffset();
    }

    void constrainOffset()
    {
        if (m_targetSize.isEmpty() || width() <= 0 || height() <= 0)
            return;

        const QSizeF scaledSize(m_targetSize.width() * m_zoom,
                                m_targetSize.height() * m_zoom);
        const QPointF baseTopLeft((width() - m_targetSize.width()) / 2.0,
                                  (height() - m_targetSize.height()) / 2.0);
        const qreal minX = width() - scaledSize.width() - baseTopLeft.x();
        const qreal maxX = -baseTopLeft.x();
        const qreal minY = height() - scaledSize.height() - baseTopLeft.y();
        const qreal maxY = -baseTopLeft.y();
        m_offset.setX(qBound(minX, m_offset.x(), maxX));
        m_offset.setY(qBound(minY, m_offset.y(), maxY));
    }

    QPixmap m_source;
    QPixmap m_scaled;
    QSize m_targetSize;
    double m_zoom = 1.0;
    QPointF m_offset{0, 0};
    bool m_dragging = false;
    QPoint m_lastMousePos;
};

// Multiplayer uses free-for-all rules, so this is intentionally framed as
// "you vs. the leading opponent" rather than presenting fictional teams.
class BattleHudWidget : public QWidget
{
public:
    // 参战者条目统一定义在 GameWindow 里（3-4 人局一行平分）。
    using Fighter = GameWindow::HudFighter;

    explicit BattleHudWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedHeight(76);
    }

    // 静态刷新（不带动画）。
    void setFighters(const QVector<Fighter> &fighters, double multiplier)
    {
        m_fighters = fighters;
        for (Fighter &fighter : m_fighters) {
            fighter.hp = qBound(0, fighter.hp, GameWindow::MP_START_HP);
            fighter.startHp = fighter.hp;
            fighter.endHp = fighter.hp;
            fighter.alive = fighter.hp > 0;
        }
        m_multiplier = multiplier;
        m_hitFlash = 0.0;
        if (m_hpAnimation)
            m_hpAnimation->stop();
        update();
    }

    // 带动画刷新：每名玩家从 startHp 掉到 hp。
    void animateFighters(const QVector<Fighter> &fighters, double multiplier)
    {
        m_fighters = fighters;
        m_multiplier = multiplier;
        for (Fighter &fighter : m_fighters) {
            fighter.startHp = qBound(0, fighter.startHp, GameWindow::MP_START_HP);
            fighter.endHp = qBound(0, fighter.hp, GameWindow::MP_START_HP);
            fighter.alive = fighter.endHp > 0;
        }

        if (!m_hpAnimation) {
            m_hpAnimation = new QVariantAnimation(this);
            connect(m_hpAnimation, &QVariantAnimation::valueChanged, this,
                    [this](const QVariant &value) {
                const qreal t = value.toReal();
                for (Fighter &fighter : m_fighters)
                    fighter.hp = qRound(fighter.startHp + (fighter.endHp - fighter.startHp) * t);
                m_hitFlash = 1.0 - t;
                update();
            });
            connect(m_hpAnimation, &QVariantAnimation::finished, this, [this]() {
                for (Fighter &fighter : m_fighters)
                    fighter.hp = fighter.endHp;
                m_hitFlash = 0.0;
                update();
            });
        }
        m_hpAnimation->stop();
        m_hpAnimation->setStartValue(0.0);
        m_hpAnimation->setEndValue(1.0);
        m_hpAnimation->setDuration(1150);
        m_hpAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_hpAnimation->start();
    }

    // 每个座位的主题色：优先用玩家自己选的颜色，没选才用内置座位色。
    static QColor seatColor(int index, bool local)
    {
        if (local)
            return QColor(46, 120, 221);
        static const QColor palette[] = {
            QColor(210, 61, 77), QColor(217, 119, 6), QColor(124, 92, 255)};
        return palette[qBound(0, index - 1, 2)];
    }

    static QColor fighterColor(const Fighter &fighter, int index)
    {
        if (!fighter.color.isEmpty())
            return QColor(fighter.color);
        return seatColor(index, fighter.local);
    }

    // 每名玩家那一格的中心 x（控件坐标）：结算时扣血数字按这个位置飞向对应血条。
    QVector<qreal> cellCentersX() const
    {
        QVector<qreal> out;
        const QRectF frame = rect().adjusted(2, 5, -2, -5);
        const int count = m_fighters.size();
        if (count <= 0 || frame.width() <= 0.0)
            return out;
        if (count <= 2) {
            out.append(frame.left() + frame.width() * 0.25);
            if (count == 2)
                out.append(frame.left() + frame.width() * 0.75);
            return out;
        }
        const qreal rowWidth = qMax<qreal>(200.0, frame.width() - kChipWidth);
        const qreal cellWidth = rowWidth / count;
        for (int i = 0; i < count; ++i)
            out.append(frame.left() + (i + 0.5) * cellWidth);
        return out;
    }

    // 每名玩家的血条矩形（控件坐标）：分数卡就摆在这几个矩形正下方。
    QVector<QRectF> barRects() const
    {
        QVector<QRectF> out;
        const QRectF frame = rect().adjusted(2, 5, -2, -5);
        const int count = m_fighters.size();
        if (count <= 0 || frame.width() <= 0.0)
            return out;
        if (count <= 2) {
            const qreal middle = frame.center().x();
            const qreal gap = 106.0;
            const qreal avatar = 50.0;
            out.append(QRectF(frame.left() + avatar - 3, frame.top() + 14,
                              middle - gap / 2.0 - (frame.left() + avatar - 3), 39));
            if (count == 2)
                out.append(QRectF(middle + gap / 2.0, frame.top() + 14,
                                  frame.right() - avatar + 3 - (middle + gap / 2.0), 39));
            return out;
        }
        const qreal rowWidth = qMax<qreal>(200.0, frame.width() - kChipWidth);
        const qreal cellWidth = rowWidth / count;
        const qreal avatar = 40.0;
        for (int i = 0; i < count; ++i) {
            const QRectF cell(frame.left() + i * cellWidth, frame.top(),
                              cellWidth - 8.0, frame.height());
            const QRectF avatarRect(cell.left() + 2.0, cell.top() + 8.0, avatar, avatar);
            out.append(QRectF(avatarRect.right() + 8.0, cell.top() + 8.0,
                              qMax<qreal>(30.0, cell.width() - avatar - 12.0), 38.0));
        }
        return out;
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        if (m_fighters.isEmpty())
            return;

        const QRectF frame = rect().adjusted(2, 5, -2, -5);
        const bool duel = m_fighters.size() <= 2;

        auto drawBar = [this, &p](const QRectF &bar, int hp, const QColor &edge) {
            if (bar.width() < 40.0)
                return;
            const qreal ratio = qBound(0.0, hp / double(GameWindow::MP_START_HP), 1.0);
            QLinearGradient panelGradient(bar.topLeft(), bar.bottomLeft());
            panelGradient.setColorAt(0.0, QColor(21, 28, 38, 238));
            panelGradient.setColorAt(1.0, QColor(8, 12, 19, 238));
            const QColor flashEdge = m_hitFlash > 0.05
                ? QColor::fromRgbF(1.0, 0.28, 0.28, qMin(1.0, 0.25 + m_hitFlash))
                : edge;
            p.setPen(QPen(flashEdge, 2.0));
            p.setBrush(panelGradient);
            p.drawRoundedRect(bar, 9.0, 9.0);

            const QRectF inner = bar.adjusted(6.0, 6.0, -6.0, -6.0);
            const QRectF fill(inner.left(), inner.top(), inner.width() * ratio, inner.height());
            if (fill.width() > 1.0) {
                QLinearGradient hpGradient(fill.topLeft(), fill.bottomLeft());
                hpGradient.setColorAt(0.0, QColor(174, 255, 71));
                hpGradient.setColorAt(0.55, QColor(93, 221, 37));
                hpGradient.setColorAt(1.0, QColor(51, 174, 27));
                p.setPen(Qt::NoPen);
                p.setBrush(hpGradient);
                p.drawRoundedRect(fill, 5.0, 5.0);
            }

            QFont valueFont = p.font();
            valueFont.setPointSize(17);
            valueFont.setWeight(QFont::Bold);
            valueFont.setItalic(true);
            p.setFont(valueFont);
            p.setPen(Qt::white);
            p.drawText(inner, Qt::AlignCenter, QString::number(hp));
        };

        auto drawAvatar = [&p](const QRectF &r, const QColor &color, int symbolSize) {
            p.setPen(QPen(QColor(237, 245, 255), 2.0));
            p.setBrush(color);
            p.drawEllipse(r);
            QFont f = p.font();
            f.setPointSize(symbolSize);
            f.setWeight(QFont::Bold);
            f.setItalic(false);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(r, Qt::AlignCenter, QStringLiteral("⌖"));
        };

        auto drawMultiplier = [this, &p](const QRectF &box) {
            p.setPen(QPen(QColor(129, 146, 165), 1.0));
            p.setBrush(QColor(9, 14, 21, 220));
            p.drawRoundedRect(box, 14, 14);
            QFont font = p.font();
            font.setPointSize(12);
            font.setWeight(QFont::Bold);
            font.setItalic(false);
            p.setFont(font);
            p.setPen(QColor(244, 199, 94));
            p.drawText(box, Qt::AlignCenter,
                       QStringLiteral("×%1").arg(m_multiplier, 0, 'f', 1));
        };

        if (duel) {
            // ---- 2 人：保持原来的一左一右 ----
            const qreal middle = frame.center().x();
            const qreal gap = 106.0;
            const qreal avatar = 50.0;
            const QRectF leftBar(frame.left() + avatar - 3, frame.top() + 14,
                                 middle - gap / 2.0 - (frame.left() + avatar - 3), 39);
            const QRectF rightBar(middle + gap / 2.0, frame.top() + 14,
                                  frame.right() - avatar + 3 - (middle + gap / 2.0), 39);

            for (int i = 0; i < m_fighters.size(); ++i) {
                const Fighter &fighter = m_fighters.at(i);
                const bool left = (i == 0);
                const QRectF bar = left ? leftBar : rightBar;
                if (bar.width() < 60.0)
                    continue;
                const QColor edge = fighter.local ? QColor(91, 120, 255) : QColor(239, 82, 95);
                drawBar(bar, fighter.hp, edge);

                QFont captionFont = p.font();
                captionFont.setPointSize(9);
                captionFont.setWeight(QFont::DemiBold);
                p.setFont(captionFont);
                p.setPen(QColor(188, 202, 215));
                const QRectF captionRect(bar.left(), bar.top() - 15, bar.width(), 13);
                p.drawText(captionRect,
                           left ? Qt::AlignLeft | Qt::AlignVCenter
                                : Qt::AlignRight | Qt::AlignVCenter,
                           fighter.local ? QStringLiteral("我的生命") : QStringLiteral("对手"));

                QFont nameFont = p.font();
                nameFont.setPointSize(10);
                nameFont.setWeight(QFont::DemiBold);
                p.setFont(nameFont);
                p.setPen(QColor(232, 239, 245));
                const QRectF nameRect(bar.left(), bar.bottom() + 3, bar.width(), 15);
                p.drawText(nameRect,
                           left ? Qt::AlignLeft | Qt::AlignVCenter
                                : Qt::AlignRight | Qt::AlignVCenter,
                           fighter.name);
            }

            const QRectF leftAvatar(frame.left(), frame.top() + 8, avatar, avatar);
            const QRectF rightAvatar(frame.right() - avatar, frame.top() + 8, avatar, avatar);
            drawAvatar(leftAvatar, fighterColor(m_fighters.at(0), 0), 20);
            if (m_fighters.size() > 1)
                drawAvatar(rightAvatar, fighterColor(m_fighters.at(1), 1), 20);
            drawMultiplier(QRectF(middle - 44, frame.top() + 9, 88, 42));
            return;
        }

        // ---- 3-4 人：顶上一行平分，每格 = 头像 + 右边血条 ----
        const int count = m_fighters.size();
        const qreal rowWidth = qMax<qreal>(200.0, frame.width() - kChipWidth);
        const qreal cellWidth = rowWidth / count;
        const qreal avatar = 40.0;
        for (int i = 0; i < count; ++i) {
            const Fighter &fighter = m_fighters.at(i);
            const QRectF cell(frame.left() + i * cellWidth, frame.top(),
                              cellWidth - 8.0, frame.height());
            const QRectF avatarRect(cell.left() + 2.0, cell.top() + 8.0, avatar, avatar);
            const QRectF bar(avatarRect.right() + 8.0, cell.top() + 8.0,
                             qMax<qreal>(30.0, cell.width() - avatar - 12.0), 38.0);
            const QColor color = fighterColor(fighter, i);
            drawAvatar(avatarRect, color, 16);
            drawBar(bar, fighter.hp, color.lighter(125));

            QFont nameFont = p.font();
            nameFont.setPointSize(9);
            nameFont.setWeight(QFont::DemiBold);
            nameFont.setItalic(false);
            p.setFont(nameFont);
            p.setPen(fighter.alive ? QColor(232, 239, 245) : QColor(248, 113, 113));
            const QRectF nameRect(bar.left(), bar.bottom() + 2.0, bar.width(), 14.0);
            p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                       fighter.alive ? fighter.name
                                     : QStringLiteral("%1 · 淘汰").arg(fighter.name));
        }
        drawMultiplier(QRectF(frame.right() - kChipWidth + 6.0, frame.top() + 8.0,
                              kChipWidth - 12.0, 38.0));
    }

private:
    static constexpr qreal kChipWidth = 64.0;

    QVector<Fighter> m_fighters;
    double m_multiplier = 1.0;
    qreal m_hitFlash = 0.0;
    QVariantAnimation *m_hpAnimation = nullptr;
};

// 最后 15 秒的红色边缘警报：中间透明、边缘偏红，透明度来回波动（视频里那种警报滤镜）。
class AlarmOverlayWidget : public QWidget
{
public:
    explicit AlarmOverlayWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        m_tick = new QTimer(this);
        m_tick->setInterval(33);
        connect(m_tick, &QTimer::timeout, this, [this]() {
            // 每 2 秒一个完整周期；用余弦让首尾接得上（不会从最亮突然掉到最暗）。
            const qreal phase = qreal(m_clock.elapsed() % 2000) / 2000.0;
            m_t = 0.5 - 0.5 * std::cos(phase * 2.0 * M_PI);
            update();
        });
        hide();
    }

    void setActive(bool active)
    {
        if (active == m_active)
            return;
        m_active = active;
        if (active) {
            m_clock.restart();
            m_t = 0.0;
            m_tick->start();
            show();
            raise();
        } else {
            m_tick->stop();
            hide();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        if (!m_active)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect();
        if (r.isEmpty())
            return;

        // 时深时浅：连续的正弦波动（每 2 秒一个周期），只保留柔和的径向渐晕。
        const qreal strength = 0.05 + 0.22 * m_t;
        QRadialGradient gradient(r.center(), qMax(r.width(), r.height()) * 0.62);
        gradient.setColorAt(0.00, QColor(170, 15, 25, 0));
        gradient.setColorAt(0.55, QColor(190, 20, 30, int(255 * strength * 0.45)));
        gradient.setColorAt(1.00, QColor(220, 25, 40, int(255 * strength)));
        p.setPen(Qt::NoPen);
        p.setBrush(gradient);
        p.drawRect(rect());
    }

private:
    QTimer *m_tick = nullptr;
    QElapsedTimer m_clock;
    qreal m_t = 0.0;
    bool m_active = false;
};

GameWindow::~GameWindow()
{
    delete m_bank;
    m_bank = nullptr;
}

GameWindow::GameWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("异环图寻 · NTE SceneSeeker"));
    setMinimumSize(960, 600);
    setFocusPolicy(Qt::StrongFocus);
    loadSettings();
    setStyleSheet(QStringLiteral(
        "QWidget { font-family: 'Inter', 'Sora', 'Microsoft YaHei UI', 'Segoe UI', sans-serif; }"
        "QPushButton#submitButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #38BDF8, stop:1 #22D3EE);"
        "  color: #0B0E14; border: none; border-radius: 8px;"
        "  font-size: 15px; font-weight: 700; padding: 10px 36px;"
        "}"
        "QPushButton#submitButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4FD1C5, stop:1 #38BDF8); }"
        "QPushButton#submitButton:pressed { background: #22D3EE; }"
        "QPushButton#submitButton:disabled { background: rgba(56,189,248,32); color: #6f8790; }"
    ));

    // 主视图容器：当前仅提供 2D 截图模式（全景/3D 已移至 archive/pano_3d）。
    m_viewStack = new QStackedWidget(this);
    m_viewStack->setObjectName(QStringLiteral("viewStack"));
    m_viewStack->setFrameShape(QFrame::NoFrame);
    m_viewStack->setAttribute(Qt::WA_StyledBackground, false);

    m_screenshot = new ScreenshotView(m_viewStack);
    m_viewStack->addWidget(m_screenshot);

    m_viewStack->setCurrentWidget(m_screenshot);


    // 地图缩为右下角的半透明入口，不再遮挡多人对战血条。
    m_mapButton = new QPushButton(QStringLiteral("⌖  地图"), this);
    m_mapButton->setObjectName(QStringLiteral("mapButton"));
    m_mapButton->setToolTip(QStringLiteral("打开地图并标记位置"));
    m_mapButton->setCursor(Qt::PointingHandCursor);
    m_mapButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: rgba(8,15,27,135); color: #E0F2FE;"
        " border: 1px solid rgba(103,232,249,120); border-radius: 18px;"
        " padding: 8px 16px; font-weight: 700; }"
        "QPushButton:hover { background: rgba(14,116,144,190); border-color: #A5F3FC; }"
        "QPushButton:pressed { background: rgba(8,47,73,220); }"));
    m_mapButton->hide();

    m_hud = new QWidget(this);
    m_hud->setObjectName(QStringLiteral("hudBar"));
    m_hud->setAttribute(Qt::WA_StyledBackground, true);
    m_hud->setStyleSheet(QStringLiteral(
        "QWidget#hudBar {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(11,14,20,235), stop:1 rgba(15,23,42,245));"
        "  border: 1px solid rgba(56,189,248,55);"
        "  border-radius: 12px;"
        "}"
        "QLabel { background: transparent; }"
    ));

    auto *hudLayout = new QHBoxLayout(m_hud);
    hudLayout->setContentsMargins(24, 0, 20, 0);
    hudLayout->setSpacing(16);

    m_roundLabel = new QLabel(QStringLiteral("第 1/5 回合"), m_hud);
    m_roundLabel->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #F8FAFC; letter-spacing: 0.5px;"));
    hudLayout->addWidget(m_roundLabel);

    hudLayout->addStretch(1);

    m_submitButton = new QPushButton(QStringLiteral("提交答案"), m_hud);
    m_submitButton->setObjectName(QStringLiteral("submitButton"));
    m_submitButton->setMinimumSize(150, 44);
    m_submitButton->setCursor(Qt::PointingHandCursor);
    hudLayout->addWidget(m_submitButton);

    m_noticeLabel = new QLabel(this);
    m_noticeLabel->setObjectName(QStringLiteral("noticeLabel"));
    m_noticeLabel->setAttribute(Qt::WA_StyledBackground, true);
    m_noticeLabel->setTextFormat(Qt::PlainText);
    m_noticeLabel->setWordWrap(true);
    m_noticeLabel->setAlignment(Qt::AlignCenter);
    m_noticeLabel->setStyleSheet(QStringLiteral(
        "QLabel#noticeLabel {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(30,41,59,242), stop:1 rgba(15,23,42,235));"
        "  color: #F97316;"
        "  border: 1px solid rgba(249,115,22,180);"
        "  border-radius: 8px;"
        "  padding: 10px 18px;"
        "  font-size: 13px; font-weight: 600;"
        "}"
    ));
    m_noticeLabel->hide();

    m_overlay = new MapOverlay(this);

    // 独立的顶部倒计时胶囊：不随 HUD 被地图覆盖层挡住，打开地图后仍然可见。
    m_topTimer = new QLabel(this);
    m_topTimer->setObjectName(QStringLiteral("topTimerChip"));
    m_topTimer->setAttribute(Qt::WA_StyledBackground, true);
    m_topTimer->setAlignment(Qt::AlignCenter);
    m_topTimer->setText(QStringLiteral("剩余 01:00"));
    m_topTimer->setStyleSheet(QStringLiteral(
        "QLabel#topTimerChip {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(11,14,20,195), stop:1 rgba(15,23,42,215));"
        "  color: #38BDF8;"
        "  border: 1px solid rgba(56,189,248,110);"
        "  border-radius: 16px;"
        "  padding: 8px 22px;"
        "  font-family: 'Cascadia Mono', 'Consolas', 'SF Mono', monospace;"
        "  font-size: 19px; font-weight: 700; letter-spacing: 1px;"
        "}"
    ));
    m_topTimer->adjustSize();
    m_topTimer->hide();

    // 回合间的大号倒计时（3 → 2 → 1），盖在地图覆盖层之上。
    m_countdownLabel = new QLabel(this);
    m_countdownLabel->setAlignment(Qt::AlignCenter);
    m_countdownLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_countdownLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #F8FAFC; background: rgba(8,15,27,150);"
        " border: 2px solid rgba(103,232,249,130); border-radius: 28px;"
        " font-size: 150px; font-weight: 900; }"));
    m_countdownLabel->hide();

    // 掉线等待遮罩：盖在地图/画面上，提示正在等待重连。
    m_pauseLabel = new QLabel(this);
    m_pauseLabel->setAlignment(Qt::AlignCenter);
    m_pauseLabel->setWordWrap(true);
    m_pauseLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #F8FAFC; background: rgba(4,8,14,232);"
        " border: none; padding: 24px; font-size: 24px; font-weight: 800; }"));
    m_pauseLabel->hide();

    // 房主专用：掉线等待期间可以不等满 30 秒，直接结束等待。
    m_pauseSkipButton = new QPushButton(QStringLiteral("结束等待"), this);
    m_pauseSkipButton->setCursor(Qt::PointingHandCursor);
    m_pauseSkipButton->setToolTip(QStringLiteral("立刻结束等待，把掉线玩家判为弃赛并继续对局"));
    m_pauseSkipButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #22D3EE; color: #04141B; border: none;"
        " border-radius: 10px; font-size: 17px; font-weight: 800; padding: 12px 34px; }"
        "QPushButton:hover { background: #67E8F9; }"
        "QPushButton:pressed { background: #06B6D4; }"));
    m_pauseSkipButton->hide();
    connect(m_pauseSkipButton, &QPushButton::clicked, this, &GameWindow::finishMpPauseWait);

    // 多人对阵 HUD：本地玩家对当前领先对手，视觉参考竞速/对战游戏血条。
    m_mpHud = new BattleHudWidget(this);
    m_mpHud->setMinimumWidth(680);
    m_mpHud->setMaximumWidth(1180);
    m_mpHud->hide();

    // 多人回合结算面板。
    m_mpResultPanel = new QWidget(this);
    m_mpResultPanel->setObjectName(QStringLiteral("mpResultPanel"));
    m_mpResultPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_mpResultPanel->setStyleSheet(QStringLiteral(
        "QWidget#mpResultPanel { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(11,14,20,248), stop:1 rgba(15,23,42,252)); color: #F8FAFC; }"
        "QLabel { background: transparent; }"
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #38BDF8, stop:1 #22D3EE); color: #0B0E14; border: none;"
        "  border-radius: 8px; padding: 12px 34px; font-size: 16px; font-weight: 800; }"
        "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4FD1C5, stop:1 #38BDF8); }"
        "QPushButton:disabled { background: rgba(56,189,248,38); color: #6f8790; }"
    ));
    auto *mpResultLayout = new QVBoxLayout(m_mpResultPanel);
    mpResultLayout->setContentsMargins(80, 50, 80, 50);
    mpResultLayout->setSpacing(14);
    mpResultLayout->addStretch(1);
    m_mpResultLabel = new QLabel(m_mpResultPanel);
    m_mpResultLabel->setAlignment(Qt::AlignCenter);
    m_mpResultLabel->setWordWrap(true);
    m_mpResultLabel->setTextFormat(Qt::RichText);
    m_mpResultLabel->setStyleSheet(QStringLiteral("font-size: 15px; line-height: 1.8;"));
    mpResultLayout->addWidget(m_mpResultLabel);
    m_mpContinueButton = new QPushButton(QStringLiteral("继续"), m_mpResultPanel);
    m_mpContinueButton->setMinimumSize(180, 46);
    m_mpContinueButton->setCursor(Qt::PointingHandCursor);
    mpResultLayout->addWidget(m_mpContinueButton, 0, Qt::AlignHCenter);
    mpResultLayout->addStretch(1);
    m_mpResultPanel->hide();

    // 结算页。
    m_summaryPage = new QWidget(this);
    m_summaryPage->setStyleSheet(QStringLiteral(
        "QWidget { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0B0E14, stop:1 #111827); color: #F8FAFC; }"
        "QLabel { background: transparent; }"
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #38BDF8, stop:1 #22D3EE);"
        "  color: #0B0E14; border: none; border-radius: 8px;"
        "  padding: 12px 32px; font-size: 15px; font-weight: 700;"
        "}"
        "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4FD1C5, stop:1 #38BDF8); }"
        "QPushButton#exitButton {"
        "  background: rgba(30,41,59,95); color: #F8FAFC;"
        "  border: 1px solid rgba(56,189,248,90);"
        "}"
        "QPushButton#exitButton:hover { background: rgba(56,189,248,22); }"
        "QScrollArea { background: transparent; border: none; }"
    ));
    m_summaryPage->hide();

    auto *summaryLayout = new QVBoxLayout(m_summaryPage);
    summaryLayout->setContentsMargins(72, 46, 72, 36);
    summaryLayout->setSpacing(12);

    auto *summaryTitle = new QLabel(QStringLiteral("本局结算"), m_summaryPage);
    summaryTitle->setAlignment(Qt::AlignCenter);
    summaryTitle->setStyleSheet(QStringLiteral("font-size: 32px; font-weight: 800; color: #F8FAFC; letter-spacing: 2px;"));
    summaryLayout->addWidget(summaryTitle);

    m_summaryRatingLabel = new QLabel(QStringLiteral("C"), m_summaryPage);
    m_summaryRatingLabel->setAlignment(Qt::AlignCenter);
    summaryLayout->addWidget(m_summaryRatingLabel);

    m_summaryTotalLabel = new QLabel(QStringLiteral("总分 0 / 25000"), m_summaryPage);
    m_summaryTotalLabel->setAlignment(Qt::AlignCenter);
    m_summaryTotalLabel->setStyleSheet(QStringLiteral("font-size: 19px; color: #cfe2e9;"));
    summaryLayout->addWidget(m_summaryTotalLabel);

    summaryLayout->addSpacing(8);

    auto *scroll = new QScrollArea(m_summaryPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *rowsWidget = new QWidget(scroll);
    rowsWidget->setStyleSheet(QStringLiteral("background: transparent;"));
    m_summaryRowsLayout = new QVBoxLayout(rowsWidget);
    m_summaryRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_summaryRowsLayout->setSpacing(8);
    scroll->setWidget(rowsWidget);
    summaryLayout->addWidget(scroll, 1);

    auto *summaryButtons = new QHBoxLayout();
    summaryButtons->addStretch(1);
    auto *restartButton = new QPushButton(QStringLiteral("再来一局"), m_summaryPage);
    restartButton->setMinimumSize(150, 46);
    restartButton->setCursor(Qt::PointingHandCursor);
    summaryButtons->addWidget(restartButton);
    auto *exitButton = new QPushButton(QStringLiteral("返回主菜单"), m_summaryPage);
    exitButton->setObjectName(QStringLiteral("exitButton"));
    exitButton->setMinimumSize(160, 46);
    exitButton->setCursor(Qt::PointingHandCursor);
    summaryButtons->addWidget(exitButton);
    summaryButtons->addStretch(1);
    summaryLayout->addLayout(summaryButtons);

    connect(restartButton, &QPushButton::clicked, this, &GameWindow::startNewGame);
    connect(exitButton, &QPushButton::clicked, this, &GameWindow::showStartMenu);

    // 开始界面：杂志封面式背景轮播 + 底部文字导航。
    m_startPage = new QWidget(this);
    m_startPage->setObjectName(QStringLiteral("startPage"));
    m_startPage->setAttribute(Qt::WA_StyledBackground, true);
    m_startPage->setStyleSheet(QStringLiteral(
        "QWidget#startPage { background: #05070A; color: #F8FAFC; }"
        "QLabel { background: transparent; }"
        "QStackedWidget#startStack, QWidget#mainMenuPage { background: transparent; }"
        "QPushButton#coverNav {"
        "  background: transparent; color: #D7E1EA; border: none;"
        "  border-bottom: 1px solid transparent;"
        "  padding: 9px 34px 9px 0; font-size: 17px; font-weight: 600;"
        "  letter-spacing: 0.5px; text-align: left;"
        "}"
        "QPushButton#coverNav:hover { color: #FFFFFF; border-bottom-color: #38BDF8; }"
        "QPushButton#coverNav:pressed { color: #7DD3FC; }"
        "QPushButton#coverNav:focus { color: #FFFFFF; border-bottom-color: #38BDF8; outline: none; }"
        "QPushButton#menuPrimary {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #38BDF8, stop:1 #22D3EE);"
        "  color: #0B0E14; border: none; border-radius: 10px;"
        "  padding: 14px 56px; font-size: 18px; font-weight: 800; letter-spacing: 0.5px;"
        "}"
        "QPushButton#menuPrimary:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4FD1C5, stop:1 #38BDF8); }"
        "QPushButton#menuPrimary:pressed { background: #22D3EE; }"
        "QPushButton#menuSecondary {"
        "  background: rgba(30,41,59,85); color: #F8FAFC;"
        "  border: 1px solid rgba(56,189,248,75); border-radius: 8px;"
        "  padding: 11px 38px; font-size: 15px; font-weight: 600;"
        "}"
        "QPushButton#menuSecondary:hover { background: rgba(56,189,248,28); border-color: #38BDF8; }"
        "QPushButton#modeButton {"
        "  background: rgba(30,41,59,75); color: #F8FAFC;"
        "  border: 1px solid rgba(56,189,248,65); border-radius: 10px;"
        "  padding: 14px 24px; font-size: 16px; font-weight: 600; text-align: center;"
        "}"
        "QPushButton#modeButton:hover { background: rgba(56,189,248,24); border-color: #38BDF8; }"
        "QPushButton#modeButton:pressed { background: rgba(56,189,248,42); }"
        "QPushButton#backButton {"
        "  background: transparent; color: #8ba0ad; border: none;"
        "  padding: 8px 20px; font-size: 13px;"
        "}"
        "QPushButton#backButton:hover { color: #F8FAFC; }"
        "QLabel#modeHint { color: #F97316; font-size: 13px; font-weight: 600; }"
    ));

    m_startBackground = new BackgroundCarousel(m_startPage);
    connect(m_startBackground, &BackgroundCarousel::slideChanged, this,
            [this](int index, const QString &caption) {
        if (m_coverCaption)
            m_coverCaption->setText(caption.isEmpty() ? QStringLiteral("海特洛市") : caption);
        if (m_coverIndexLabel) {
            m_coverIndexLabel->setText(QStringLiteral("%1 / %2")
                .arg(index + 1, 2, 10, QLatin1Char('0'))
                .arg(qMax(1, m_startBackground->count()), 2, 10, QLatin1Char('0')));
        }
    });

    auto *startOuterLayout = new QVBoxLayout(m_startPage);
    startOuterLayout->setContentsMargins(0, 0, 0, 0);
    m_startStack = new QStackedWidget(m_startPage);
    m_startStack->setObjectName(QStringLiteral("startStack"));
    m_startStack->setAttribute(Qt::WA_StyledBackground, false);
    startOuterLayout->addWidget(m_startStack);

    // 主菜单：杂志封面式排版（大标题压在图上，导航在底部一行）
    m_mainMenuPage = new QWidget(m_startStack);
    auto *mainLayout = new QVBoxLayout(m_mainMenuPage);
    mainLayout->setContentsMargins(96, 46, 96, 42);
    mainLayout->setSpacing(0);

    auto *mastheadRow = new QHBoxLayout();
    auto *masthead = new QLabel(QStringLiteral("N T E   S C E N E   S E E K E R"), m_mainMenuPage);
    masthead->setStyleSheet(QStringLiteral("font-size: 11px; color: #9FB3C8; letter-spacing: 3px; font-weight: 700;"));
    mastheadRow->addWidget(masthead);
    mastheadRow->addStretch(1);
    auto *issueLabel = new QLabel(QStringLiteral("VOL. 01  /  CITY ARCHIVE"), m_mainMenuPage);
    issueLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #7C8FA5; letter-spacing: 2px;"));
    mastheadRow->addWidget(issueLabel);
    mainLayout->addLayout(mastheadRow);

    mainLayout->addStretch(3);

    auto *startTitle = new CoverTitleWidget(QStringLiteral("异环图寻"), m_mainMenuPage);
    startTitle->setMinimumWidth(660);
    mainLayout->addWidget(startTitle);

    auto *titleRule = new QFrame(m_mainMenuPage);
    titleRule->setFixedHeight(2);
    titleRule->setStyleSheet(QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #38BDF8, stop:1 transparent);"));
    titleRule->setMaximumWidth(520);
    mainLayout->addWidget(titleRule);

    auto *startSub = new QLabel(QStringLiteral("还记着这里是哪里吗"), m_mainMenuPage);
    startSub->setStyleSheet(QStringLiteral("font-size: 16px; color: #D5E1EA; margin-top: 16px;"));
    mainLayout->addWidget(startSub);

    mainLayout->addSpacing(38);

    auto *navLayout = new QHBoxLayout();
    navLayout->setSpacing(4);
    auto *singleButton = new QPushButton(QStringLiteral("01　单人游戏"), m_mainMenuPage);
    singleButton->setObjectName(QStringLiteral("coverNav"));
    singleButton->setCursor(Qt::PointingHandCursor);
    singleButton->setFocus();
    navLayout->addWidget(singleButton);

    auto *multiButton = new QPushButton(QStringLiteral("02　多人游戏"), m_mainMenuPage);
    multiButton->setObjectName(QStringLiteral("coverNav"));
    multiButton->setCursor(Qt::PointingHandCursor);
    navLayout->addWidget(multiButton);

    auto *settingsButton = new QPushButton(QStringLiteral("03　设置"), m_mainMenuPage);
    settingsButton->setObjectName(QStringLiteral("coverNav"));
    settingsButton->setCursor(Qt::PointingHandCursor);
    navLayout->addWidget(settingsButton);

    auto *quitButton = new QPushButton(QStringLiteral("04　退出游戏"), m_mainMenuPage);
    quitButton->setObjectName(QStringLiteral("coverNav"));
    quitButton->setCursor(Qt::PointingHandCursor);
    navLayout->addWidget(quitButton);
    navLayout->addStretch(1);
    mainLayout->addLayout(navLayout);

    mainLayout->addSpacing(22);

    auto *captionRow = new QHBoxLayout();
    m_coverCaption = new QLabel(QStringLiteral("海特洛市"), m_mainMenuPage);
    m_coverCaption->setStyleSheet(QStringLiteral("font-size: 12px; color: #8FA6B8; letter-spacing: 2px;"));
    captionRow->addWidget(m_coverCaption);
    captionRow->addStretch(1);
    auto *keyHint = new QLabel(QStringLiteral("F11 全屏　·　Esc 返回菜单"), m_mainMenuPage);
    keyHint->setStyleSheet(QStringLiteral("font-size: 12px; color: #7C8FA5; letter-spacing: 1px;"));
    captionRow->addWidget(keyHint);
    captionRow->addSpacing(18);
    m_coverIndexLabel = new QLabel(QStringLiteral("01 / 04"), m_mainMenuPage);
    m_coverIndexLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #8FA6B8; letter-spacing: 2px;"));
    captionRow->addWidget(m_coverIndexLabel);
    mainLayout->addLayout(captionRow);

    m_startStack->addWidget(m_mainMenuPage);

    if (m_coverCaption) {
        const QString caption = m_startBackground->currentCaption();
        m_coverCaption->setText(caption.isEmpty() ? QStringLiteral("海特洛市") : caption);
    }
    if (m_coverIndexLabel) {
        m_coverIndexLabel->setText(QStringLiteral("%1 / %2")
            .arg(1, 2, 10, QLatin1Char('0'))
            .arg(qMax(1, m_startBackground->count()), 2, 10, QLatin1Char('0')));
    }

    // 单人模式选择
    m_singlePlayerPage = new QWidget(m_startStack);
    auto *modeLayout = new QVBoxLayout(m_singlePlayerPage);
    modeLayout->setContentsMargins(80, 54, 80, 50);
    modeLayout->setSpacing(12);
    modeLayout->addStretch(1);

    auto *modeTitle = new QLabel(QStringLiteral("单人游戏"), m_singlePlayerPage);
    modeTitle->setAlignment(Qt::AlignCenter);
    modeTitle->setStyleSheet(QStringLiteral("font-size: 40px; font-weight: 800; color: #eefaff;"));
    modeLayout->addWidget(modeTitle);

    auto *modeSub = new QLabel(QStringLiteral("看截图，在街道地图上找出拍摄位置"), m_singlePlayerPage);
    modeSub->setAlignment(Qt::AlignCenter);
    modeSub->setStyleSheet(QStringLiteral("font-size: 15px; color: #8ba0ad; margin-bottom: 14px;"));
    modeLayout->addWidget(modeSub);

    auto *photoModeButton = new QPushButton(
        QStringLiteral("开始游戏"), m_singlePlayerPage);
    photoModeButton->setObjectName(QStringLiteral("modeButton"));
    photoModeButton->setMinimumSize(460, 78);
    photoModeButton->setCursor(Qt::PointingHandCursor);
    modeLayout->addWidget(photoModeButton, 0, Qt::AlignHCenter);

    m_modeHintLabel = new QLabel(QString(), m_singlePlayerPage);
    m_modeHintLabel->setObjectName(QStringLiteral("modeHint"));
    m_modeHintLabel->setAlignment(Qt::AlignCenter);
    m_modeHintLabel->setWordWrap(true);
    m_modeHintLabel->hide();
    modeLayout->addWidget(m_modeHintLabel);

    modeLayout->addSpacing(4);
    auto *modeBackButton = new QPushButton(QStringLiteral("返回主菜单"), m_singlePlayerPage);
    modeBackButton->setObjectName(QStringLiteral("backButton"));
    modeBackButton->setCursor(Qt::PointingHandCursor);
    modeLayout->addWidget(modeBackButton, 0, Qt::AlignHCenter);
    modeLayout->addStretch(1);
    m_startStack->addWidget(m_singlePlayerPage);

    // 多人游戏入口：创建房间 / 加入房间
    m_multiplayerPage = new QWidget(m_startStack);
    auto *multiLayout = new QVBoxLayout(m_multiplayerPage);
    multiLayout->setContentsMargins(80, 54, 80, 50);
    multiLayout->setSpacing(12);
    multiLayout->addStretch(1);

    auto *multiTitle = new QLabel(QStringLiteral("多人游戏"), m_multiplayerPage);
    multiTitle->setAlignment(Qt::AlignCenter);
    multiTitle->setStyleSheet(QStringLiteral("font-size: 40px; font-weight: 800; color: #eefaff;"));
    multiLayout->addWidget(multiTitle);

    auto *multiSub = new QLabel(QStringLiteral("局域网 2–4 人房间"), m_multiplayerPage);
    multiSub->setAlignment(Qt::AlignCenter);
    multiSub->setStyleSheet(QStringLiteral("font-size: 15px; color: #8ba0ad; margin-bottom: 14px;"));
    multiLayout->addWidget(multiSub);

    auto *createRoomButton = new QPushButton(QStringLiteral("创建房间"), m_multiplayerPage);
    createRoomButton->setObjectName(QStringLiteral("menuPrimary"));
    createRoomButton->setMinimumSize(300, 54);
    createRoomButton->setCursor(Qt::PointingHandCursor);
    multiLayout->addWidget(createRoomButton, 0, Qt::AlignHCenter);

    auto *joinRoomButton = new QPushButton(QStringLiteral("加入房间"), m_multiplayerPage);
    joinRoomButton->setObjectName(QStringLiteral("menuSecondary"));
    joinRoomButton->setMinimumSize(300, 48);
    joinRoomButton->setCursor(Qt::PointingHandCursor);
    multiLayout->addWidget(joinRoomButton, 0, Qt::AlignHCenter);

    auto *multiHint = new QLabel(QStringLiteral("房主创建后会显示局域网地址和随机端口"), m_multiplayerPage);
    multiHint->setAlignment(Qt::AlignCenter);
    multiHint->setStyleSheet(QStringLiteral("font-size: 12px; color: #5f737e; margin-top: 12px;"));
    multiLayout->addWidget(multiHint);

    auto *multiBackButton = new QPushButton(QStringLiteral("返回主菜单"), m_multiplayerPage);
    multiBackButton->setObjectName(QStringLiteral("backButton"));
    multiBackButton->setCursor(Qt::PointingHandCursor);
    multiLayout->addWidget(multiBackButton, 0, Qt::AlignHCenter);
    multiLayout->addStretch(1);
    m_startStack->addWidget(m_multiplayerPage);

    // 创建房间
    m_mpCreatePage = new QWidget(m_startStack);
    auto *mpCreateLayout = new QVBoxLayout(m_mpCreatePage);
    mpCreateLayout->setContentsMargins(80, 48, 80, 46);
    mpCreateLayout->setSpacing(10);
    mpCreateLayout->addStretch(1);
    auto *mpCreateTitle = new QLabel(QStringLiteral("创建房间"), m_mpCreatePage);
    mpCreateTitle->setAlignment(Qt::AlignCenter);
    mpCreateTitle->setStyleSheet(QStringLiteral("font-size: 34px; font-weight: 800; color: #eefaff;"));
    mpCreateLayout->addWidget(mpCreateTitle);
    auto *mpCreateSub = new QLabel(QStringLiteral("设置房间规则，创建后把地址发给好友"), m_mpCreatePage);
    mpCreateSub->setAlignment(Qt::AlignCenter);
    mpCreateSub->setStyleSheet(QStringLiteral("font-size: 13px; color: #8ba0ad; margin-bottom: 8px;"));
    mpCreateLayout->addWidget(mpCreateSub);

    auto *mpCreateFormHost = new QWidget(m_mpCreatePage);
    mpCreateFormHost->setMaximumWidth(560);
    auto *mpCreateForm = new QFormLayout(mpCreateFormHost);
    mpCreateForm->setLabelAlignment(Qt::AlignRight);
    mpCreateForm->setFormAlignment(Qt::AlignHCenter);
    mpCreateForm->setHorizontalSpacing(16);
    mpCreateForm->setVerticalSpacing(10);
    m_mpCreateName = new QLineEdit(mpCreateFormHost);
    m_mpCreateName->setPlaceholderText(QStringLiteral("你的名字"));
    m_mpCreateName->setMinimumWidth(240);
    mpCreateForm->addRow(QStringLiteral("玩家名字"), m_mpCreateName);
    m_mpModeCombo = new QComboBox(mpCreateFormHost);
    m_mpModeCombo->addItems({QStringLiteral("2D 截图")});
    m_mpModeCombo->setCurrentIndex(0);
    m_mpModeCombo->setMinimumWidth(240);
    mpCreateForm->addRow(QStringLiteral("游戏模式"), m_mpModeCombo);
    m_mpRoundsSpin = new QSpinBox(mpCreateFormHost);
    m_mpRoundsSpin->setRange(MIN_ROUNDS, MAX_ROUNDS);
    m_mpRoundsSpin->setValue(DEFAULT_ROUNDS);
    m_mpRoundsSpin->setSuffix(QStringLiteral(" 局"));
    m_mpRoundsSpin->setMinimumWidth(240);
    mpCreateForm->addRow(QStringLiteral("回合数"), m_mpRoundsSpin);
    m_mpSecondsSpin = new QSpinBox(mpCreateFormHost);
    m_mpSecondsSpin->setRange(15, 300);
    m_mpSecondsSpin->setValue(DEFAULT_SECONDS_PER_ROUND);
    m_mpSecondsSpin->setSuffix(QStringLiteral(" 秒"));
    m_mpSecondsSpin->setMinimumWidth(240);
    mpCreateForm->addRow(QStringLiteral("每局时间"), m_mpSecondsSpin);
    m_mpPortSpin = new QSpinBox(mpCreateFormHost);
    m_mpPortSpin->setRange(0, 65535);
    m_mpPortSpin->setValue(46000);
    m_mpPortSpin->setSpecialValueText(QStringLiteral("自动"));
    m_mpPortSpin->setMinimumWidth(240);
    m_mpPortSpin->setToolTip(QStringLiteral("公网 / FRP 联机时请固定端口，并让转发端口与之一致。"));
    mpCreateForm->addRow(QStringLiteral("监听端口"), m_mpPortSpin);
    mpCreateLayout->addWidget(mpCreateFormHost, 0, Qt::AlignHCenter);

    auto *portHint = new QLabel(
        QStringLiteral("局域网可保持默认；用 FRP / 端口转发时，转发端口要和这里一致。"),
        m_mpCreatePage);
    portHint->setAlignment(Qt::AlignCenter);
    portHint->setWordWrap(true);
    portHint->setStyleSheet(QStringLiteral("font-size: 12px; color: #8FA6B8;"));
    mpCreateLayout->addWidget(portHint);
    m_mpCreateStatus = new QLabel(QString(), m_mpCreatePage);
    m_mpCreateStatus->setAlignment(Qt::AlignCenter);
    m_mpCreateStatus->setWordWrap(true);
    m_mpCreateStatus->setStyleSheet(QStringLiteral("font-size: 12px; color: #ffb86b;"));
    mpCreateLayout->addWidget(m_mpCreateStatus);
    auto *createRoomConfirm = new QPushButton(QStringLiteral("创建房间"), m_mpCreatePage);
    createRoomConfirm->setObjectName(QStringLiteral("menuPrimary"));
    createRoomConfirm->setMinimumSize(260, 50);
    createRoomConfirm->setCursor(Qt::PointingHandCursor);
    mpCreateLayout->addWidget(createRoomConfirm, 0, Qt::AlignHCenter);
    auto *mpCreateBack = new QPushButton(QStringLiteral("返回"), m_mpCreatePage);
    mpCreateBack->setObjectName(QStringLiteral("backButton"));
    mpCreateBack->setCursor(Qt::PointingHandCursor);
    mpCreateLayout->addWidget(mpCreateBack, 0, Qt::AlignHCenter);
    mpCreateLayout->addStretch(1);
    m_startStack->addWidget(m_mpCreatePage);

    // 加入房间
    m_mpJoinPage = new QWidget(m_startStack);
    auto *mpJoinLayout = new QVBoxLayout(m_mpJoinPage);
    mpJoinLayout->setContentsMargins(80, 48, 80, 46);
    mpJoinLayout->setSpacing(10);
    mpJoinLayout->addStretch(1);
    auto *mpJoinTitle = new QLabel(QStringLiteral("加入房间"), m_mpJoinPage);
    mpJoinTitle->setAlignment(Qt::AlignCenter);
    mpJoinTitle->setStyleSheet(QStringLiteral("font-size: 34px; font-weight: 800; color: #eefaff;"));
    mpJoinLayout->addWidget(mpJoinTitle);
    auto *mpJoinSub = new QLabel(QStringLiteral("输入房主提供的 IP:端口 或网址"), m_mpJoinPage);
    mpJoinSub->setAlignment(Qt::AlignCenter);
    mpJoinSub->setStyleSheet(QStringLiteral("font-size: 13px; color: #8ba0ad; margin-bottom: 8px;"));
    mpJoinLayout->addWidget(mpJoinSub);
    auto *mpJoinFormHost = new QWidget(m_mpJoinPage);
    mpJoinFormHost->setMaximumWidth(560);
    auto *mpJoinForm = new QFormLayout(mpJoinFormHost);
    mpJoinForm->setLabelAlignment(Qt::AlignRight);
    mpJoinForm->setFormAlignment(Qt::AlignHCenter);
    mpJoinForm->setHorizontalSpacing(16);
    mpJoinForm->setVerticalSpacing(10);
    m_mpJoinName = new QLineEdit(mpJoinFormHost);
    m_mpJoinName->setPlaceholderText(QStringLiteral("你的名字"));
    m_mpJoinName->setMinimumWidth(300);
    mpJoinForm->addRow(QStringLiteral("玩家名字"), m_mpJoinName);
    m_mpJoinAddress = new QLineEdit(mpJoinFormHost);
    m_mpJoinAddress->setPlaceholderText(QStringLiteral("例如 192.168.1.23:52143"));
    m_mpJoinAddress->setMinimumWidth(300);
    mpJoinForm->addRow(QStringLiteral("房间地址"), m_mpJoinAddress);
    mpJoinLayout->addWidget(mpJoinFormHost, 0, Qt::AlignHCenter);
    m_mpJoinStatus = new QLabel(QString(), m_mpJoinPage);
    m_mpJoinStatus->setAlignment(Qt::AlignCenter);
    m_mpJoinStatus->setWordWrap(true);
    m_mpJoinStatus->setStyleSheet(QStringLiteral("font-size: 12px; color: #ffb86b;"));
    mpJoinLayout->addWidget(m_mpJoinStatus);
    auto *joinRoomConfirm = new QPushButton(QStringLiteral("加入"), m_mpJoinPage);
    joinRoomConfirm->setObjectName(QStringLiteral("menuPrimary"));
    joinRoomConfirm->setMinimumSize(260, 50);
    joinRoomConfirm->setCursor(Qt::PointingHandCursor);
    mpJoinLayout->addWidget(joinRoomConfirm, 0, Qt::AlignHCenter);
    auto *mpJoinBack = new QPushButton(QStringLiteral("返回"), m_mpJoinPage);
    mpJoinBack->setObjectName(QStringLiteral("backButton"));
    mpJoinBack->setCursor(Qt::PointingHandCursor);
    mpJoinLayout->addWidget(mpJoinBack, 0, Qt::AlignHCenter);
    mpJoinLayout->addStretch(1);
    m_startStack->addWidget(m_mpJoinPage);

    // 三栏房间大厅：玩家状态 / 聊天 / 房间设置与准备。
    m_mpLobbyPage = new QWidget(m_startStack);
    auto *mpLobbyLayout = new QVBoxLayout(m_mpLobbyPage);
    mpLobbyLayout->setContentsMargins(42, 28, 42, 30);
    mpLobbyLayout->setSpacing(14);
    auto *mpLobbyHeader = new QHBoxLayout();
    auto *mpLobbyTitle = new QLabel(QStringLiteral("联机房间"), m_mpLobbyPage);
    mpLobbyTitle->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 800; color: #F8FAFC; letter-spacing: 2px;"));
    mpLobbyHeader->addWidget(mpLobbyTitle);
    mpLobbyHeader->addStretch(1);
    mpLobbyLayout->addLayout(mpLobbyHeader);

    const QString panelStyle = QStringLiteral("background: rgba(15,23,42,205); border: 1px solid rgba(56,189,248,75); border-radius: 12px;");
    auto *lobbyColumns = new QHBoxLayout();
    lobbyColumns->setSpacing(14);

    auto *playersPanel = new QWidget(m_mpLobbyPage);
    playersPanel->setStyleSheet(panelStyle);
    playersPanel->setMinimumWidth(250);
    playersPanel->setMaximumWidth(300);
    auto *playersLayout = new QVBoxLayout(playersPanel);
    playersLayout->setContentsMargins(16, 16, 16, 16);
    auto *playersTitle = new QLabel(QStringLiteral("在线玩家"), playersPanel);
    playersTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 800; color: #F8FAFC;"));
    playersLayout->addWidget(playersTitle);
    m_mpLobbyPlayers = new QLabel(playersPanel);
    m_mpLobbyPlayers->setTextFormat(Qt::RichText);
    m_mpLobbyPlayers->setAlignment(Qt::AlignTop);
    m_mpLobbyPlayers->setWordWrap(true);
    m_mpLobbyPlayers->setStyleSheet(QStringLiteral("font-size: 14px; color: #dceaf0;"));
    playersLayout->addWidget(m_mpLobbyPlayers, 1);
    auto *mpLobbyLeave = new QPushButton(QStringLiteral("离开房间"), playersPanel);
    mpLobbyLeave->setObjectName(QStringLiteral("menuSecondary"));
    mpLobbyLeave->setCursor(Qt::PointingHandCursor);
    playersLayout->addWidget(mpLobbyLeave);
    lobbyColumns->addWidget(playersPanel, 3);

    auto *chatPanel = new QWidget(m_mpLobbyPage);
    chatPanel->setStyleSheet(panelStyle);
    auto *chatLayout = new QVBoxLayout(chatPanel);
    chatLayout->setContentsMargins(16, 16, 16, 16);
    auto *chatTitle = new QLabel(QStringLiteral("房间聊天"), chatPanel);
    chatTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 800; color: #F8FAFC;"));
    chatLayout->addWidget(chatTitle);
    m_mpChatHistory = new QTextEdit(chatPanel);
    m_mpChatHistory->setReadOnly(true);
    m_mpChatHistory->setStyleSheet(QStringLiteral("QTextEdit { background: rgba(2,6,23,150); color: #dceaf0; border: 1px solid rgba(148,163,184,45); border-radius: 8px; padding: 8px; font-size: 13px; }"));
    chatLayout->addWidget(m_mpChatHistory, 1);
    auto *chatInputLayout = new QHBoxLayout();
    m_mpChatInput = new QLineEdit(chatPanel);
    m_mpChatInput->setPlaceholderText(QStringLiteral("输入消息，按 Enter 发送…"));
    m_mpChatInput->setMaxLength(220);
    m_mpChatInput->setStyleSheet(QStringLiteral("QLineEdit { background: rgba(2,6,23,190); color: #F8FAFC; border: 1px solid rgba(56,189,248,90); border-radius: 7px; padding: 9px; }"));
    chatInputLayout->addWidget(m_mpChatInput, 1);
    m_mpChatSendButton = new QPushButton(QStringLiteral("发送"), chatPanel);
    m_mpChatSendButton->setObjectName(QStringLiteral("menuPrimary"));
    m_mpChatSendButton->setCursor(Qt::PointingHandCursor);
    m_mpChatSendButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #E2E8F0; color: #172033; border: 1px solid #CBD5E1;"
        " border-radius: 7px; padding: 8px 14px; font-weight: 800; }"
        "QPushButton:hover { background: #F8FAFC; }"
        "QPushButton:pressed { background: #CBD5E1; }"));
    chatInputLayout->addWidget(m_mpChatSendButton);
    chatLayout->addLayout(chatInputLayout);
    lobbyColumns->addWidget(chatPanel, 6);

    auto *rulesPanel = new QWidget(m_mpLobbyPage);
    rulesPanel->setStyleSheet(panelStyle);
    rulesPanel->setMinimumWidth(280);
    rulesPanel->setMaximumWidth(350);
    auto *rulesLayout = new QVBoxLayout(rulesPanel);
    rulesLayout->setContentsMargins(16, 16, 16, 16);
    auto *rulesTitle = new QLabel(QStringLiteral("对局设置"), rulesPanel);
    rulesTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 800; color: #F8FAFC;"));
    rulesLayout->addWidget(rulesTitle);
    m_mpLobbySettingsPanel = new QWidget(rulesPanel);
    auto *mpLobbySettingsForm = new QFormLayout(m_mpLobbySettingsPanel);
    mpLobbySettingsForm->setContentsMargins(0, 4, 0, 4);
    mpLobbySettingsForm->setLabelAlignment(Qt::AlignRight);
    mpLobbySettingsForm->setHorizontalSpacing(12);
    mpLobbySettingsForm->setVerticalSpacing(10);
    m_mpLobbyModeCombo = new QComboBox(m_mpLobbySettingsPanel);
    m_mpLobbyModeCombo->addItems({QStringLiteral("2D 截图")});
    mpLobbySettingsForm->addRow(QStringLiteral("模式"), m_mpLobbyModeCombo);
    m_mpLobbyRoundsSpin = new QSpinBox(m_mpLobbySettingsPanel);
    m_mpLobbyRoundsSpin->setRange(MIN_ROUNDS, MAX_ROUNDS);
    m_mpLobbyRoundsSpin->setSuffix(QStringLiteral(" 局"));
    mpLobbySettingsForm->addRow(QStringLiteral("回合"), m_mpLobbyRoundsSpin);
    m_mpLobbySecondsSpin = new QSpinBox(m_mpLobbySettingsPanel);
    m_mpLobbySecondsSpin->setRange(15, 300);
    m_mpLobbySecondsSpin->setSuffix(QStringLiteral(" 秒"));
    mpLobbySettingsForm->addRow(QStringLiteral("每局时间"), m_mpLobbySecondsSpin);
    m_mpApplySettingsButton = new QPushButton(QStringLiteral("应用设置"), m_mpLobbySettingsPanel);
    m_mpApplySettingsButton->setObjectName(QStringLiteral("menuSecondary"));
    m_mpApplySettingsButton->setCursor(Qt::PointingHandCursor);
    mpLobbySettingsForm->addRow(QString(), m_mpApplySettingsButton);
    rulesLayout->addWidget(m_mpLobbySettingsPanel);

    // 颜色选择：房间内互斥，别人选了的置灰。
    auto *colorTitle = new QLabel(QStringLiteral("我的颜色"), rulesPanel);
    colorTitle->setStyleSheet(QStringLiteral(
        "font-size: 13px; font-weight: 700; color: #CBD5E1; margin-top: 4px;"));
    rulesLayout->addWidget(colorTitle);
    m_mpColorGrid = new QWidget(rulesPanel);
    auto *colorGrid = new QGridLayout(m_mpColorGrid);
    colorGrid->setContentsMargins(0, 0, 0, 0);
    colorGrid->setSpacing(5);
    const QStringList colorPalette = MultiplayerSession::colorPalette();
    for (int i = 0; i < colorPalette.size(); ++i) {
        auto *swatch = new QPushButton(m_mpColorGrid);
        swatch->setFixedSize(26, 26);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setToolTip(QStringLiteral("选择这个颜色"));
        swatch->setProperty("colorHex", colorPalette.at(i));
        connect(swatch, &QPushButton::clicked, this, [this, i]() { chooseMpColor(i); });
        colorGrid->addWidget(swatch, i / 8, i % 8);
        m_mpColorSwatches.append(swatch);
    }
    rulesLayout->addWidget(m_mpColorGrid);
    rulesLayout->addStretch(1);
    m_mpLobbyStatus = new QLabel(QString(), rulesPanel);
    m_mpLobbyStatus->setWordWrap(true);
    m_mpLobbyStatus->setAlignment(Qt::AlignCenter);
    m_mpLobbyStatus->setStyleSheet(QStringLiteral("font-size: 12px; color: #FBBF24;"));
    rulesLayout->addWidget(m_mpLobbyStatus);
    m_mpReadyButton = new QPushButton(QStringLiteral("准备"), rulesPanel);
    m_mpReadyButton->setObjectName(QStringLiteral("menuPrimary"));
    m_mpReadyButton->setMinimumHeight(48);
    m_mpReadyButton->setCursor(Qt::PointingHandCursor);
    m_mpReadyButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #16A34A; color: white; border: 1px solid #4ADE80;"
        " border-radius: 8px; font-weight: 800; font-size: 15px; }"
        "QPushButton:hover { background: #22C55E; }"
        "QPushButton:pressed { background: #15803D; }"));
    rulesLayout->addWidget(m_mpReadyButton);
    m_mpStartButton = new QPushButton(QStringLiteral("开始游戏"), rulesPanel);
    m_mpStartButton->setObjectName(QStringLiteral("menuPrimary"));
    m_mpStartButton->setMinimumHeight(48);
    m_mpStartButton->setCursor(Qt::PointingHandCursor);
    m_mpStartButton->setStyleSheet(QStringLiteral(
        "QPushButton { background: #16A34A; color: white; border: 1px solid #4ADE80;"
        " border-radius: 8px; font-weight: 800; font-size: 15px; }"
        "QPushButton:hover { background: #22C55E; }"
        "QPushButton:pressed { background: #15803D; }"
        "QPushButton:disabled { background: rgba(22,163,74,75); color: #BBF7D0; border-color: rgba(74,222,128,75); }"));
    rulesLayout->addWidget(m_mpStartButton);
    lobbyColumns->addWidget(rulesPanel, 3);
    mpLobbyLayout->addLayout(lobbyColumns, 1);
    auto *mpLobbyBack = new QPushButton(QStringLiteral("返回主菜单"), m_mpLobbyPage);
    mpLobbyBack->setObjectName(QStringLiteral("backButton"));
    mpLobbyBack->setCursor(Qt::PointingHandCursor);
    mpLobbyLayout->addWidget(mpLobbyBack, 0, Qt::AlignHCenter);
    m_startStack->addWidget(m_mpLobbyPage);

    // 设置页
    m_settingsPage = new QWidget(m_startStack);
    auto *settingsLayout = new QVBoxLayout(m_settingsPage);
    settingsLayout->setContentsMargins(80, 54, 80, 50);
    settingsLayout->setSpacing(12);
    settingsLayout->addStretch(1);

    auto *settingsTitle = new QLabel(QStringLiteral("设置"), m_settingsPage);
    settingsTitle->setAlignment(Qt::AlignCenter);
    settingsTitle->setStyleSheet(QStringLiteral("font-size: 40px; font-weight: 800; color: #eefaff;"));
    settingsLayout->addWidget(settingsTitle);

    auto *settingsSub = new QLabel(QStringLiteral("游戏规则"), m_settingsPage);
    settingsSub->setAlignment(Qt::AlignCenter);
    settingsSub->setStyleSheet(QStringLiteral("font-size: 15px; color: #8ba0ad; margin-bottom: 14px;"));
    settingsLayout->addWidget(settingsSub);

    auto *settingsFormHost = new QWidget(m_settingsPage);
    settingsFormHost->setMaximumWidth(520);
    auto *settingsForm = new QFormLayout(settingsFormHost);
    settingsForm->setLabelAlignment(Qt::AlignRight);
    settingsForm->setFormAlignment(Qt::AlignHCenter);
    settingsForm->setHorizontalSpacing(20);
    settingsForm->setVerticalSpacing(12);
    settingsForm->setContentsMargins(0, 0, 0, 0);

    m_roundsSpinBox = new QSpinBox(settingsFormHost);
    m_roundsSpinBox->setRange(MIN_ROUNDS, MAX_ROUNDS);
    m_roundsSpinBox->setValue(m_roundsPerGame);
    m_roundsSpinBox->setSuffix(QStringLiteral(" 回合"));
    m_roundsSpinBox->setMinimumWidth(180);
    settingsForm->addRow(QStringLiteral("每局回合数"), m_roundsSpinBox);

    m_timerCheckBox = new QCheckBox(QStringLiteral("启用倒计时"), settingsFormHost);
    m_timerCheckBox->setChecked(m_timerEnabled);
    settingsForm->addRow(QString(), m_timerCheckBox);

    m_fullscreenCheckBox = new QCheckBox(QStringLiteral("全屏启动（游戏中 F11 可随时切换）"), settingsFormHost);
    settingsForm->addRow(QString(), m_fullscreenCheckBox);

    m_secondsSpinBox = new QSpinBox(settingsFormHost);
    m_secondsSpinBox->setRange(MIN_SECONDS_PER_ROUND, MAX_SECONDS_PER_ROUND);
    m_secondsSpinBox->setValue(m_secondsPerRound);
    m_secondsSpinBox->setSuffix(QStringLiteral(" 秒"));
    m_secondsSpinBox->setMinimumWidth(180);
    m_secondsSpinBox->setEnabled(m_timerEnabled);
    settingsForm->addRow(QStringLiteral("每回合时间"), m_secondsSpinBox);

    m_musicSlider = new QSlider(Qt::Horizontal, settingsFormHost);
    m_musicSlider->setRange(0, 100);
    m_musicSlider->setValue(m_musicVolume);
    m_musicSlider->setMinimumWidth(220);
    m_musicSlider->setToolTip(QStringLiteral("背景音乐音量"));
    settingsForm->addRow(QStringLiteral("音乐音量"), m_musicSlider);

    m_sfxSlider = new QSlider(Qt::Horizontal, settingsFormHost);
    m_sfxSlider->setRange(0, 100);
    m_sfxSlider->setValue(m_sfxVolume);
    m_sfxSlider->setMinimumWidth(220);
    m_sfxSlider->setToolTip(QStringLiteral("按钮与提示音效音量"));
    settingsForm->addRow(QStringLiteral("音效音量"), m_sfxSlider);

    connect(m_musicSlider, &QSlider::valueChanged, this, [this](int value) {
        m_musicVolume = value;
        if (m_bgm)
            m_bgm->setVolume(value / 100.0);
    });
    connect(m_sfxSlider, &QSlider::valueChanged, this, [this](int value) {
        m_sfxVolume = value;
        Sfx::setVolume(value / 100.0);
    });

    settingsLayout->addWidget(settingsFormHost, 0, Qt::AlignHCenter);

    auto *settingsHint = new QLabel(
        QStringLiteral("设置从下一局开始生效；题库类型仍由游戏模式决定。"), m_settingsPage);
    settingsHint->setAlignment(Qt::AlignCenter);
    settingsHint->setStyleSheet(QStringLiteral("font-size: 12px; color: #5f737e; margin-top: 6px;"));
    settingsLayout->addWidget(settingsHint);

    auto *settingsButtons = new QHBoxLayout();
    settingsButtons->addStretch(1);
    auto *resetSettingsButton = new QPushButton(QStringLiteral("恢复默认"), m_settingsPage);
    resetSettingsButton->setObjectName(QStringLiteral("menuSecondary"));
    resetSettingsButton->setMinimumSize(140, 44);
    resetSettingsButton->setCursor(Qt::PointingHandCursor);
    settingsButtons->addWidget(resetSettingsButton);
    auto *saveSettingsButton = new QPushButton(QStringLiteral("保存"), m_settingsPage);
    saveSettingsButton->setObjectName(QStringLiteral("menuPrimary"));
    saveSettingsButton->setMinimumSize(160, 46);
    saveSettingsButton->setCursor(Qt::PointingHandCursor);
    settingsButtons->addWidget(saveSettingsButton);
    settingsButtons->addStretch(1);
    settingsLayout->addLayout(settingsButtons);

    auto *settingsBackButton = new QPushButton(QStringLiteral("返回主菜单"), m_settingsPage);
    settingsBackButton->setObjectName(QStringLiteral("backButton"));
    settingsBackButton->setCursor(Qt::PointingHandCursor);
    settingsLayout->addWidget(settingsBackButton, 0, Qt::AlignHCenter);
    settingsLayout->addStretch(1);
    m_startStack->addWidget(m_settingsPage);

    m_startStack->setCurrentWidget(m_mainMenuPage);

    connect(singleButton, &QPushButton::clicked, this, &GameWindow::showSinglePlayerMenu);
    connect(multiButton, &QPushButton::clicked, this, &GameWindow::showMultiplayerMenu);
    connect(settingsButton, &QPushButton::clicked, this, &GameWindow::showSettingsMenu);
    connect(quitButton, &QPushButton::clicked, qApp, []() { QCoreApplication::quit(); });
    connect(createRoomButton, &QPushButton::clicked, this, &GameWindow::showMpCreatePage);
    connect(joinRoomButton, &QPushButton::clicked, this, &GameWindow::showMpJoinPage);
    connect(createRoomConfirm, &QPushButton::clicked, this, &GameWindow::createMpRoom);
    connect(joinRoomConfirm, &QPushButton::clicked, this, &GameWindow::joinMpRoom);
    connect(mpCreateBack, &QPushButton::clicked, this, &GameWindow::showMultiplayerMenu);
    connect(mpJoinBack, &QPushButton::clicked, this, &GameWindow::showMultiplayerMenu);
    connect(m_mpStartButton, &QPushButton::clicked, this, &GameWindow::startMpMatch);
    connect(m_mpApplySettingsButton, &QPushButton::clicked, this, &GameWindow::applyMpLobbySettings);
    connect(m_mpReadyButton, &QPushButton::clicked, this, &GameWindow::toggleMpReady);
    connect(m_mpChatSendButton, &QPushButton::clicked, this, &GameWindow::sendMpChat);
    connect(m_mpChatInput, &QLineEdit::returnPressed, this, &GameWindow::sendMpChat);
    connect(mpLobbyLeave, &QPushButton::clicked, this, &GameWindow::leaveMpRoom);
    connect(mpLobbyBack, &QPushButton::clicked, this, &GameWindow::leaveMpRoom);
    connect(m_mpContinueButton, &QPushButton::clicked, this, [this]() {
        if (m_mpMatchEnded) {
            returnMpToLobby();
            return;
        }
        if (m_mpIsHost)
            startMpRound(m_mpRoundIndex + 1);
    });
    connect(photoModeButton, &QPushButton::clicked, this, [this]() {
        startNewGameWithMode(GameMode::PhotoOnly);
    });
    connect(modeBackButton, &QPushButton::clicked, this, &GameWindow::showStartMenu);
    connect(multiBackButton, &QPushButton::clicked, this, &GameWindow::showStartMenu);
    connect(m_timerCheckBox, &QCheckBox::toggled, m_secondsSpinBox, &QWidget::setEnabled);
    connect(resetSettingsButton, &QPushButton::clicked, this, [this]() {
        m_roundsSpinBox->setValue(DEFAULT_ROUNDS);
        m_secondsSpinBox->setValue(DEFAULT_SECONDS_PER_ROUND);
        m_timerCheckBox->setChecked(true);
    });
    connect(saveSettingsButton, &QPushButton::clicked, this, &GameWindow::saveSettings);
    connect(settingsBackButton, &QPushButton::clicked, this, &GameWindow::showStartMenu);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        // 掉线等待期间计时完全冻结：不推进剩余时间，也不触发回合结算。
        if (!m_roundActive || m_mpPaused)
            return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 remainingMs = m_roundDeadlineMs - now;
        m_remainingSeconds = int(std::max<qint64>(0, (remainingMs + 999) / 1000));
        refreshHud();
        if (m_remainingSeconds <= 0) {
            m_timer->stop();
            if (m_mpActive) {
                if (m_mpIsHost)
                    resolveMpRound();
                return;
            }
            if (!m_hasGuess) {
                m_guess = QPointF(0.5, 0.5);
                m_hasGuess = true;
            }
            submitAnswer();
        }
    });

    connect(m_mapButton, &QPushButton::clicked, this, &GameWindow::openMapForPick);
    connect(m_overlay, &MapOverlay::pinSelected, this, [this](const QPointF &normalized) {
        m_guess = normalized;
        m_hasGuess = true;
        if (m_mpActive) {
            if (m_mpIsHost) {
                handleMpPin(m_mpLocalId, normalized);
            } else {
                m_mpSession->send(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("pin")},
                    {QStringLiteral("round"), m_mpRoundIndex},
                    {QStringLiteral("point"), QJsonObject{
                        {QStringLiteral("x"), normalized.x()},
                        {QStringLiteral("y"), normalized.y()},
                    }},
                });
            }
        }
    });
    connect(m_submitButton, &QPushButton::clicked, this, [this]() {
        if (!m_hasGuess) {
            openMapForPick();
            return;
        }
        submitAnswer();
    });

    connect(m_overlay, &MapOverlay::locationSelected, this, [this](const QPointF &normalized) {
        m_guess = normalized;
        m_hasGuess = true;
        resumeTimerAfterOverlay();
    });
    connect(m_overlay, &MapOverlay::cancelled, this, &GameWindow::resumeTimerAfterOverlay);
    connect(m_overlay, &MapOverlay::nextRequested, this, [this]() {
        // 下一题按钮时关闭地图
        if (m_overlay->isOpen())
            m_overlay->hide();
        
        if (m_roundIndex + 1 < m_roundQuestions.size())
            loadRound(m_roundIndex + 1);
        else
            showSummary();
    });

    m_mpSession = new MultiplayerSession(this);
    connect(m_mpSession, &MultiplayerSession::lobbyChanged,
            this, &GameWindow::updateMpLobby);
    connect(m_mpSession, &MultiplayerSession::roomReady, this, [this]() {
        if (!m_mpIsHost) {
            if (m_mpChatHistory)
                m_mpChatHistory->clear();
            showMpLobbyPage();
        }
    });
    connect(m_mpSession, &MultiplayerSession::messageReceived,
            this, &GameWindow::handleMpMessage);
    connect(m_mpSession, &MultiplayerSession::errorOccurred, this,
            [this](const QString &message) {
        if (m_mpJoinStatus)
            m_mpJoinStatus->setText(message);
        if (m_mpCreateStatus)
            m_mpCreateStatus->setText(message);
    });
    connect(m_mpSession, &MultiplayerSession::disconnected, this,
            [this](const QString &reason) {
        if (m_mpLobbyStatus)
            m_mpLobbyStatus->setText(reason);
        // 回主菜单后弹「重新连接」窗口：房主不弹（房主掉了就是房间没了）。
        if (!m_mpIsHost) {
            m_pendingLeaveNotice = reason;
            rememberRoomForReconnect();
        }
        leaveMpRoom();
    });

    // ---- 掉线重连（房主侧）----
    connect(m_mpSession, &MultiplayerSession::peerDisconnected, this,
            [this](int playerId, const QString &name) {
        Q_UNUSED(playerId)
        if (!m_mpActive || !m_mpIsHost)
            return;
        const qint64 remaining = qMax<qint64>(0, m_roundDeadlineMs - QDateTime::currentMSecsSinceEpoch());
        pauseMpTimer(remaining);
        m_mpSession->broadcast(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("pause")},
            {QStringLiteral("name"), name},
            {QStringLiteral("seconds"), 30},
            {QStringLiteral("remainingMs"), double(remaining)},
        });
        beginPauseCountdown(QStringLiteral("%1 掉线，等待重连…").arg(name), 30);
        qWarning().noquote() << "暂停计时，等待重连:" << name;
    });

    connect(m_mpSession, &MultiplayerSession::peerReconnected, this,
            [this](int playerId, const QString &name) {
        if (!m_mpActive || !m_mpIsHost)
            return;
        // 给刚回来的客户端补一份对局快照（它可能已经丢了界面状态）。
        if (m_mpRoundIndex >= 0 && m_mpRoundIndex < m_roundQuestions.size()) {
            QJsonArray players;
            for (const MpPlayerState &state : m_mpState) {
                players.append(QJsonObject{
                    {QStringLiteral("id"), state.id},
                    {QStringLiteral("name"), state.name},
                    {QStringLiteral("hp"), state.hp},
                    {QStringLiteral("score"), state.score},
                    {QStringLiteral("damage"), state.damage},
                    {QStringLiteral("guessX"), state.guess.x()},
                    {QStringLiteral("guessY"), state.guess.y()},
                    {QStringLiteral("submitted"), state.submitted},
                    {QStringLiteral("hasMarker"), state.hasMarker},
                    {QStringLiteral("alive"), state.hp > 0},
                });
            }
            QJsonArray questionIds;
            for (const Question &question : m_roundQuestions)
                questionIds.append(question.id);
            m_mpSession->sendTo(playerId, QJsonObject{
                {QStringLiteral("type"), QStringLiteral("snapshot")},
                {QStringLiteral("round"), m_mpRoundIndex},
                {QStringLiteral("multiplier"), m_mpMultiplier},
                {QStringLiteral("seconds"), m_mpSeconds},
                {QStringLiteral("remainingMs"), double(m_pausedRemainingMs)},
                {QStringLiteral("questionId"), m_roundQuestions.at(m_mpRoundIndex).id},
                {QStringLiteral("questions"), questionIds},
                {QStringLiteral("players"), players},
            });
        }
        bool stillWaiting = false;
        for (const MultiplayerSession::Player &player : m_mpSession->players()) {
            if (!player.host && !player.connected)
                stillWaiting = true;
        }
        if (stillWaiting)
            return;
        const qint64 remaining = m_pausedRemainingMs;
        m_mpSession->broadcast(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("resume")},
            {QStringLiteral("remainingMs"), double(remaining)},
        });
        resumeMpTimer(remaining);
        hidePauseBanner();
        qInfo().noquote() << "重连完成，继续计时:" << name;
    });

    connect(m_mpSession, &MultiplayerSession::peerDropped, this,
            [this](int playerId, const QString &name) {
        if (!m_mpActive || !m_mpIsHost)
            return;
        for (MpPlayerState &state : m_mpState) {
            if (state.id == playerId) {
                state.active = false;
                state.hp = 0;
                state.submitted = true;
            }
        }
        m_mpSession->broadcast(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("player_dropped")},
            {QStringLiteral("id"), playerId},
        });
        bool stillWaiting = false;
        for (const MultiplayerSession::Player &player : m_mpSession->players()) {
            if (!player.host && !player.connected)
                stillWaiting = true;
        }
        if (!stillWaiting) {
            const qint64 remaining = m_pausedRemainingMs;
            m_mpSession->broadcast(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("resume")},
                {QStringLiteral("remainingMs"), double(remaining)},
            });
            resumeMpTimer(remaining);
            hidePauseBanner();
        }
        qWarning().noquote() << "重连超时，判弃赛:" << name;
        updateMpHud();
        // 房间里只剩一人：对局直接结束，退回房间大厅。
        if (mpAliveCount() <= 1) {
            const QString onlyOne = (m_mpSession && m_mpSession->players().size() <= 1)
                ? QStringLiteral("房间里只剩你一人，对局结束")
                : QStringLiteral("房间里只剩 1 名玩家，对局结束");
            abortMpMatchToLobby(onlyOne);
        }
    });

    // ---- 掉线重连（客户端侧）----
    connect(m_mpSession, &MultiplayerSession::reconnecting, this,
            [this](int attempt, int maxAttempts) {
        if (!m_mpActive)
            return;
        if (!m_mpPaused)
            pauseMpTimer(qMax<qint64>(0, m_roundDeadlineMs - QDateTime::currentMSecsSinceEpoch()));
        showPauseBanner(QStringLiteral("连接中断，正在重连…（%1/%2）")
                            .arg(attempt)
                            .arg(maxAttempts));
    });

    connect(m_mpSession, &MultiplayerSession::reconnected, this, [this]() {
        if (!m_mpActive)
            return;
        showPauseBanner(QStringLiteral("已重连，等待房主恢复计时…"));
    });

    connect(m_mpSession, &MultiplayerSession::reconnectFailed, this,
            [this](const QString &reason) {
        if (!m_mpActive)
            return;
        hidePauseBanner();
        m_mpPaused = false;
        leaveMpRoom();
        showStartMenu();
        m_noticeLabel->setText(reason);
        m_noticeLabel->show();
        positionNotice();
        QTimer::singleShot(4000, m_noticeLabel, &QWidget::hide);
        qWarning().noquote() << "重连失败:" << reason;
    });

    // 房主重建了房间（认不出我们的 clientId）：退出对局回到大厅，不再卡在重连界面。
    connect(m_mpSession, &MultiplayerSession::sessionReset, this, [this]() {
        hidePauseBanner();
        m_mpPaused = false;
        m_mpActive = false;
        m_mpMatchEnded = false;
        m_mpRoundResolved = false;
        m_mpLocalSubmitted = false;
        m_mpState.clear();
        m_roundQuestions.clear();
        if (m_timer)
            m_timer->stop();
        if (m_mpHud)
            m_mpHud->hide();
        if (m_mpResultPanel)
            m_mpResultPanel->hide();
        if (m_viewStack)
            m_viewStack->hide();
        if (m_hud)
            m_hud->hide();
        if (m_topTimer)
            m_topTimer->hide();
        if (m_overlay)
            m_overlay->closeImmediately();
        if (m_countdownLabel)
            m_countdownLabel->hide();
        if (m_startPage)
            m_startPage->show();
        showMpLobbyPage();
        qWarning().noquote() << "房主已重建房间，已退出对局回到大厅";
    });

    // 背景音乐：扫描 assets/audio/bgm/ 轮换播放，音量由设置页控制。
    m_bgm = new BgmPlayer(this);
    m_bgm->setVolume(m_musicVolume / 100.0);
    m_bgm->start();

    // 最后 15 秒的红色边缘警报（盖在所有 UI 之上，鼠标穿透）。
    m_alarm = new AlarmOverlayWidget(this);

    loadQuestionBank();
    buildReconnectCard();
    showStartMenu();
}

void GameWindow::loadSettings()
{
    QSettings &settings = *PortableSettings::appSettings();
    m_roundsPerGame = qBound(
        MIN_ROUNDS,
        settings.value(QStringLiteral("game/rounds"), DEFAULT_ROUNDS).toInt(),
        MAX_ROUNDS);
    m_secondsPerRound = qBound(
        MIN_SECONDS_PER_ROUND,
        settings.value(QStringLiteral("game/secondsPerRound"), DEFAULT_SECONDS_PER_ROUND).toInt(),
        MAX_SECONDS_PER_ROUND);
    m_timerEnabled = settings.value(QStringLiteral("game/timerEnabled"), true).toBool();
    m_musicVolume = qBound(0, settings.value(QStringLiteral("audio/music"), m_musicVolume).toInt(), 100);
    m_sfxVolume = qBound(0, settings.value(QStringLiteral("audio/sfx"), m_sfxVolume).toInt(), 100);
    if (m_sfxSlider)
        m_sfxSlider->setValue(m_sfxVolume);
    Sfx::setVolume(m_sfxVolume / 100.0);
}

void GameWindow::saveSettings()
{
    if (!m_roundsSpinBox || !m_secondsSpinBox || !m_timerCheckBox)
        return;
    m_roundsPerGame = m_roundsSpinBox->value();
    m_secondsPerRound = m_secondsSpinBox->value();
    m_timerEnabled = m_timerCheckBox->isChecked();

    QSettings &settings = *PortableSettings::appSettings();
    settings.setValue(QStringLiteral("game/rounds"), m_roundsPerGame);
    settings.setValue(QStringLiteral("game/secondsPerRound"), m_secondsPerRound);
    settings.setValue(QStringLiteral("game/timerEnabled"), m_timerEnabled);
    if (m_fullscreenCheckBox)
        settings.setValue(QStringLiteral("game/fullscreen"), m_fullscreenCheckBox->isChecked());
    settings.setValue(QStringLiteral("audio/music"), m_musicVolume);
    settings.setValue(QStringLiteral("audio/sfx"), m_sfxVolume);
    settings.sync();
    showStartMenu();
}

void GameWindow::showSettingsMenu()
{
    if (!m_settingsPage || !m_roundsSpinBox || !m_secondsSpinBox || !m_timerCheckBox)
        return;
    if (m_startBackground)
        m_startBackground->setDim(0.85);
    m_roundsSpinBox->setValue(m_roundsPerGame);
    m_secondsSpinBox->setValue(m_secondsPerRound);
    m_timerCheckBox->setChecked(m_timerEnabled);
    if (m_musicSlider)
        m_musicSlider->setValue(m_musicVolume);
    if (m_sfxSlider)
        m_sfxSlider->setValue(m_sfxVolume);
    if (m_fullscreenCheckBox) {
        QSettings &settings = *PortableSettings::appSettings();
        m_fullscreenCheckBox->setChecked(
            settings.value(QStringLiteral("game/fullscreen"), true).toBool());
    }
    m_secondsSpinBox->setEnabled(m_timerEnabled);
    m_startStack->setCurrentWidget(m_settingsPage);
}

double GameWindow::questionBankMaxDistanceMeters() const
{
    // 统一口径：MaxDistance = 整张地图的对角线长度（26112 × √2 px ≈ 11.26 km）。
    return Score::metersForPixels(Score::mapDiagonalPx());
}

void GameWindow::loadQuestionBank()
{
    m_pool.clear();
    m_questionsMissing = false;
    m_bankError.clear();
    if (m_startBackground)
        m_startBackground->loadFromQuestionBank(nullptr);
    delete m_bank;
    m_bank = nullptr;

    // 1) 优先读题库包（questions.nteq + media.ntep）
    const QString bankPath = AssetPaths::assets(QStringLiteral("data/questions.nteq"));
    const QString packPath = AssetPaths::assets(QStringLiteral("data/media.ntep"));
    if (QFileInfo::exists(bankPath)) {
        auto *bank = new QuestionBank;
        QString bankError;
        if (bank->open(bankPath, packPath, &bankError)) {
            for (const BankQuestion &item : bank->approvedQuestions()) {
                if (item.type != QStringLiteral("photo") || item.mediaId.isEmpty())
                    continue;
                Question question;
                question.id = item.id;
                question.locationId = item.locationId;
                question.mediaId = item.mediaId;
                question.imageRelative = item.mediaId;   // 仅用于日志/占位提示
                question.mapVersion = item.mapVersion;
                question.district = item.district;
                question.area = item.area;
                question.title = item.title;
                question.contributor = item.contributor;
                question.contributorAnon = item.contributorAnon;
                question.photographer = item.photographer;
                question.photographerAnon = item.photographerAnon;
                question.gameX = item.gameX;
                question.gameY = item.gameY;
                question.normalized = Coordinates::clampNormalized(QPointF(item.x, item.y));
                m_pool.append(question);
            }
            if (!m_pool.isEmpty()) {
                m_bank = bank;
                qInfo().noquote() << "题库包加载完成: 可用题目" << m_pool.size()
                                  << "道 · 版本" << bank->bankVersion()
                                  << "· 包哈希" << bank->mediaPackHash().left(12)
                                  << "· 路径" << bankPath;
                if (m_startBackground)
                    m_startBackground->loadFromQuestionBank(m_bank, 12);
                return;
            }
            qWarning().noquote() << "题库包里没有可用的 2D 题目，启用占位题目";
        } else {
            qWarning().noquote() << "题库包加载失败:" << bankError;
        }
        delete bank;
    }

    // 没有可用题库包：只启用内置占位题目。
    m_questionsMissing = true;
    if (m_bankError.isEmpty())
        m_bankError = QStringLiteral("未找到可用题库包：assets/data/questions.nteq");
    buildPlaceholderBank();
}

void GameWindow::buildPlaceholderBank()
{
    m_pool.clear();
    static const QPointF points[PLACEHOLDER_ROUNDS] = {
        QPointF(0.31, 0.29),
        QPointF(0.67, 0.24),
        QPointF(0.48, 0.56),
        QPointF(0.74, 0.71),
        QPointF(0.22, 0.78),
    };
    static const char *areas[PLACEHOLDER_ROUNDS] = {
        "占位区域 A", "占位区域 B", "占位区域 C", "占位区域 D", "占位区域 E"
    };

    const int count = qMax(PLACEHOLDER_ROUNDS, m_roundsPerGame);
    for (int i = 0; i < count; ++i) {
        const int source = i % PLACEHOLDER_ROUNDS;
        Question question;
        question.id = QStringLiteral("placeholder-%1").arg(i + 1);
        question.mapVersion = QStringLiteral("1.4");
        question.district = QStringLiteral("占位测试");
        question.area = QString::fromUtf8(areas[source]);
        question.normalized = points[source];
        const QPointF game = Coordinates::normalizedToGame(points[source].x(), points[source].y());
        question.gameX = game.x();
        question.gameY = game.y();
        question.isPlaceholder = true;
        question.placeholderIndex = i;
        m_pool.append(question);
    }
}

int GameWindow::questionCountForMode(GameMode mode) const
{
    int count = 0;
    for (const Question &question : m_pool) {
        if (mode == GameMode::PhotoOnly && question.viewType != Question::ViewType::Photo)
            continue;
        if (mode == GameMode::PanoramaOnly && question.viewType != Question::ViewType::Panorama)
            continue;
        ++count;
    }
    return count;
}

void GameWindow::selectRoundQuestions()
{
    m_roundQuestions.clear();
    if (m_pool.isEmpty())
        return;

    QVector<int> indices;
    indices.reserve(m_pool.size());
    for (int i = 0; i < m_pool.size(); ++i) {
        const Question &question = m_pool.at(i);
        if (m_gameMode == GameMode::PhotoOnly && question.viewType != Question::ViewType::Photo)
            continue;
        if (m_gameMode == GameMode::PanoramaOnly && question.viewType != Question::ViewType::Panorama)
            continue;
        indices.append(i);
    }

    for (int i = indices.size() - 1; i > 0; --i) {
        const int j = QRandomGenerator::global()->bounded(i + 1);
        std::swap(indices[i], indices[j]);
    }

    const int count = std::min(m_roundsPerGame, int(indices.size()));
    m_roundQuestions.reserve(count);
    for (int i = 0; i < count; ++i)
        m_roundQuestions.append(m_pool.at(indices.at(i)));
}

void GameWindow::startGameForTesting()
{
    m_gameMode = GameMode::PhotoOnly;
    startNewGame();
}

void GameWindow::startNewGameWithMode(GameMode mode)
{
    m_gameMode = mode;
    startNewGame();
}

void GameWindow::showSinglePlayerMenu()
{
    if (!m_startStack || !m_singlePlayerPage)
        return;
    if (m_modeHintLabel)
        m_modeHintLabel->hide();
    if (m_startBackground)
        m_startBackground->setDim(0.78);
    m_startStack->setCurrentWidget(m_singlePlayerPage);
}

void GameWindow::showMultiplayerMenu()
{
    if (!m_startStack || !m_multiplayerPage)
        return;
    if (m_startBackground)
        m_startBackground->setDim(0.78);
    m_startStack->setCurrentWidget(m_multiplayerPage);
}

void GameWindow::showMpCreatePage()
{
    if (!m_mpCreatePage)
        return;
    if (m_mpCreateStatus)
        m_mpCreateStatus->clear();
    if (m_mpCreateName && m_mpCreateName->text().trimmed().isEmpty()) {
        const QString last = PortableSettings::appSettings()->value(QStringLiteral("net/lastName")).toString();
        if (!last.isEmpty())
            m_mpCreateName->setText(last);
    }
    if (m_startBackground)
        m_startBackground->setDim(0.85);
    m_startStack->setCurrentWidget(m_mpCreatePage);
}

void GameWindow::showMpJoinPage()
{
    if (!m_mpJoinPage)
        return;
    if (m_mpJoinStatus)
        m_mpJoinStatus->clear();
    // 回填上次成功进入的房间地址。
    if (m_mpJoinAddress && m_mpJoinAddress->text().trimmed().isEmpty()) {
        QSettings &settings = *PortableSettings::appSettings();
        const QString last = settings.value(QStringLiteral("net/lastAddress")).toString();
        if (!last.isEmpty())
            m_mpJoinAddress->setText(last);
    }
    if (m_mpJoinName && m_mpJoinName->text().trimmed().isEmpty()) {
        const QString lastName = PortableSettings::appSettings()->value(QStringLiteral("net/lastName")).toString();
        if (!lastName.isEmpty())
            m_mpJoinName->setText(lastName);
    }
    if (m_startBackground)
        m_startBackground->setDim(0.85);
    m_startStack->setCurrentWidget(m_mpJoinPage);
}

void GameWindow::showMpLobbyPage()
{
    if (!m_mpLobbyPage)
        return;
    if (m_mpLobbySettingsPanel) {
        m_mpLobbySettingsPanel->setVisible(true);
        if (m_mpLobbyModeCombo) {
            m_mpLobbyModeCombo->setCurrentIndex(0);
            m_mpLobbyRoundsSpin->setValue(m_mpRounds);
            m_mpLobbySecondsSpin->setValue(m_mpSeconds);
            m_mpLobbyModeCombo->setEnabled(m_mpIsHost);
            m_mpLobbyRoundsSpin->setEnabled(m_mpIsHost);
            m_mpLobbySecondsSpin->setEnabled(m_mpIsHost);
            m_mpApplySettingsButton->setVisible(m_mpIsHost);
        }
    }
    updateMpLobby();
    // 客户端进房间界面时主动要一次名单，保证颜色/名字跟房主一致。
    if (!m_mpIsHost && m_mpSession && m_mpSession->isConnected())
        m_mpSession->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("lobby_request")}});
    if (m_startBackground)
        m_startBackground->setDim(0.86);
    m_startStack->setCurrentWidget(m_mpLobbyPage);
}

void GameWindow::applyMpLobbySettings()
{
    if (!m_mpIsHost || !m_mpLobbyModeCombo || !m_mpLobbyRoundsSpin || !m_mpLobbySecondsSpin)
        return;
    m_gameMode = GameMode::PhotoOnly;
    m_mpRounds = m_mpLobbyRoundsSpin->value();
    m_mpSeconds = m_mpLobbySecondsSpin->value();
    // Changing rules invalidates the prior ready confirmation for everyone.
    if (m_mpSession)
        m_mpSession->resetReadyStates();
    updateMpLobby();
    m_mpLobbyStatus->setText(QStringLiteral("房间设置已更新"));
}

void GameWindow::appendChatSystem(const QString &text)
{
    if (!m_mpChatHistory || text.isEmpty())
        return;
    m_mpChatHistory->append(QStringLiteral(
        "<div style='color:#7C8FA5;font-size:11px;margin:2px 0'>· %1</div>")
        .arg(text.toHtmlEscaped()));
}

void GameWindow::toggleMpReady()
{
    if (m_mpIsHost || !m_mpSession || !m_mpSession->isConnected())
        return;
    bool currentlyReady = false;
    for (const MultiplayerSession::Player &player : m_mpSession->players()) {
        if (player.id == m_mpSession->localPlayerId()) {
            currentlyReady = player.ready;
            break;
        }
    }
    m_mpSession->send(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("ready")},
        {QStringLiteral("ready"), !currentlyReady},
    });
}

void GameWindow::sendMpChat()
{
    if (!m_mpSession || !m_mpChatInput)
        return;
    const QString text = m_mpChatInput->text().trimmed();
    if (text.isEmpty())
        return;
    m_mpChatInput->clear();

    const QString name = m_mpSession->localName();
    const QJsonObject chat{
        {QStringLiteral("type"), QStringLiteral("chat")},
        {QStringLiteral("name"), name},
        {QStringLiteral("text"), text.left(220)},
    };
    if (m_mpIsHost) {
        if (m_mpChatHistory)
            m_mpChatHistory->append(QStringLiteral("<span style='color:#67E8F9;font-weight:700'>%1</span>　%2")
                                      .arg(name.toHtmlEscaped(), text.toHtmlEscaped()));
        m_mpSession->broadcast(chat);
    } else {
        m_mpSession->send(chat);
    }
}

QString GameWindow::assetHash() const
{
    // 有题库包时用包里的版本信息（改题只变 bankVersion，不重算整个题库文件）
    if (m_bank) {
        const QString raw = QStringLiteral("%1|%2|%3")
                                .arg(m_bank->bankId(), m_bank->bankVersion(), m_bank->mediaPackHash());
        return QString::fromLatin1(
            QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256).toHex());
    }
    return QString();
}

QString GameWindow::bankLabel() const
{
    if (!m_bank)
        return QStringLiteral("无题库");
    if (m_bank->bankVersion().isEmpty())
        return m_bank->bankId();
    return QStringLiteral("%1 %2").arg(m_bank->bankId(), m_bank->bankVersion());
}

void GameWindow::createMpRoom()
{
    if (!m_mpCreateName || !m_mpModeCombo || !m_mpRoundsSpin || !m_mpSecondsSpin)
        return;
    const QString name = m_mpCreateName->text().trimmed();
    if (name.isEmpty()) {
        m_mpCreateStatus->setText(QStringLiteral("先输入玩家名字"));
        return;
    }
    m_mpIsHost = true;
    m_mpRounds = m_mpRoundsSpin->value();
    m_mpSeconds = m_mpSecondsSpin->value();
    m_gameMode = GameMode::PhotoOnly;
    QString error;
    const quint16 listenPort = m_mpPortSpin ? quint16(m_mpPortSpin->value()) : quint16(0);
    if (!m_mpSession->startHost(name, assetHash(), bankLabel(), &error, listenPort)) {
        m_mpIsHost = false;
        m_mpCreateStatus->setText(error);
        return;
    }
    if (m_mpChatHistory)
        m_mpChatHistory->clear();
    const QString port = m_mpSession->roomAddress().section(QLatin1Char(':'), -1);
    PortableSettings::appSettings()->setValue(QStringLiteral("net/lastName"), name);
    qInfo().noquote() << "创建房间:" << name << "· 监听端口" << port;
    appendChatSystem(QStringLiteral("房间已在端口 %1 上开放。").arg(port));
    showMpLobbyPage();
}

void GameWindow::joinMpRoom()
{
    if (!m_mpJoinName || !m_mpJoinAddress)
        return;
    const QString name = m_mpJoinName->text().trimmed();
    const QString address = m_mpJoinAddress->text().trimmed();
    if (name.isEmpty()) {
        m_mpJoinStatus->setText(QStringLiteral("先输入玩家名字"));
        return;
    }
    if (address.isEmpty()) {
        m_mpJoinStatus->setText(QStringLiteral("先输入房间地址"));
        return;
    }
    m_mpIsHost = false;
    QString error;
    if (!m_mpSession->join(name, address, assetHash(), bankLabel(), &error)) {
        m_mpJoinStatus->setText(error);
        qWarning().noquote() << "加入房间失败:" << address << "·" << error;
        return;
    }
    PortableSettings::appSettings()->setValue(QStringLiteral("net/lastAddress"), address);
    PortableSettings::appSettings()->setValue(QStringLiteral("net/lastName"), name);
    qInfo().noquote() << "加入房间:" << name << "·" << address;
    m_mpJoinStatus->setText(QStringLiteral("连接中…"));
}

void GameWindow::buildReconnectCard()
{
    if (m_reconnectCard)
        return;
    m_reconnectCard = new QWidget(this);
    m_reconnectCard->setObjectName(QStringLiteral("reconnectCard"));
    m_reconnectCard->setAttribute(Qt::WA_StyledBackground, true);
    m_reconnectCard->setStyleSheet(QStringLiteral(
        "QWidget#reconnectCard { background: rgba(8,15,27,246);"
        " border: 1px solid rgba(103,232,249,150); border-radius: 14px; }"));
    auto *layout = new QVBoxLayout(m_reconnectCard);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("连接已断开"), m_reconnectCard);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 800; color: #F8FAFC;"));
    layout->addWidget(title);

    m_reconnectLabel = new QLabel(m_reconnectCard);
    m_reconnectLabel->setWordWrap(true);
    m_reconnectLabel->setAlignment(Qt::AlignCenter);
    m_reconnectLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #CBD5E1;"));
    layout->addWidget(m_reconnectLabel);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(12);
    auto *reconnect = new QPushButton(QStringLiteral("重新连接"), m_reconnectCard);
    reconnect->setCursor(Qt::PointingHandCursor);
    reconnect->setStyleSheet(QStringLiteral(
        "QPushButton { background: #22D3EE; color: #04141B; border: none;"
        " border-radius: 8px; font-weight: 800; font-size: 14px; padding: 9px 24px; }"
        "QPushButton:hover { background: #67E8F9; }"));
    auto *dismiss = new QPushButton(QStringLiteral("关闭"), m_reconnectCard);
    dismiss->setCursor(Qt::PointingHandCursor);
    dismiss->setStyleSheet(QStringLiteral(
        "QPushButton { background: rgba(148,163,184,45); color: #E2E8F0;"
        " border: 1px solid rgba(148,163,184,90); border-radius: 8px;"
        " font-size: 14px; padding: 9px 24px; }"
        "QPushButton:hover { background: rgba(148,163,184,75); }"));
    buttons->addWidget(reconnect);
    buttons->addWidget(dismiss);
    layout->addLayout(buttons);

    connect(reconnect, &QPushButton::clicked, this, &GameWindow::reconnectToLastRoom);
    connect(dismiss, &QPushButton::clicked, this, &GameWindow::hideReconnectPrompt);
    m_reconnectCard->hide();
}

void GameWindow::updateAlarmOverlay()
{
    if (!m_alarm)
        return;
    const bool timedRound = m_roundActive && !m_mpRoundResolved
        && (m_mpActive || m_timerEnabled);
    const bool alarm = timedRound && !m_mpPaused
        && m_remainingSeconds > 0 && m_remainingSeconds <= ALARM_SECONDS;
    if (alarm) {
        m_alarm->setGeometry(rect());
        m_alarm->setActive(true);
    } else {
        m_alarm->setActive(false);
    }
}

void GameWindow::rememberRoomForReconnect()
{
    if (!m_mpSession || m_mpIsHost)
        return;
    const QString address = m_mpSession->roomAddress();
    if (address.isEmpty())
        return;
    m_lastRoomAddress = address;
    m_lastRoomName = m_mpSession->localName();
    m_lastHostName.clear();
    for (const MultiplayerSession::Player &player : m_mpSession->players()) {
        if (player.host) {
            m_lastHostName = player.name;
            break;
        }
    }
}

void GameWindow::showReconnectPrompt(const QString &reason)
{
    if (!m_reconnectCard || !m_reconnectLabel)
        return;
    // 房主不弹（房主掉了就是房间没了），没有可回的房间也不弹。
    if (m_mpIsHost || m_lastRoomAddress.isEmpty())
        return;

    // 只显示房主名字，不暴露 IP:端口（地址仍保留用于重连）。
    const QString target = m_lastHostName.isEmpty() ? m_lastRoomAddress : m_lastHostName;
    m_reconnectLabel->setText(
        QStringLiteral("%1<br/><span style='color:#94A3B8'>房主：%2</span>")
            .arg(reason.toHtmlEscaped(), target.toHtmlEscaped()));
    const int cardWidth = qMin(470, qMax(320, width() - 120));
    m_reconnectCard->setFixedWidth(cardWidth);
    m_reconnectCard->adjustSize();
    const int cardHeight = m_reconnectCard->sizeHint().height();
    m_reconnectCard->setGeometry((width() - cardWidth) / 2,
                                 qMax(20, (height() - cardHeight) / 2),
                                 cardWidth, cardHeight);
    m_reconnectCard->show();
    m_reconnectCard->raise();
}

void GameWindow::hideReconnectPrompt()
{
    if (m_reconnectCard)
        m_reconnectCard->hide();
}

void GameWindow::reconnectToLastRoom()
{
    if (m_lastRoomAddress.isEmpty())
        return;
    hideReconnectPrompt();
    if (m_mpJoinName)
        m_mpJoinName->setText(m_lastRoomName.isEmpty()
                                  ? PortableSettings::appSettings()->value(QStringLiteral("net/lastName")).toString()
                                  : m_lastRoomName);
    if (m_mpJoinAddress)
        m_mpJoinAddress->setText(m_lastRoomAddress);
    showMpJoinPage();
    joinMpRoom();
}

void GameWindow::leaveMpRoom()
{
    const bool wasClientInRoom = m_mpSession && !m_mpIsHost && m_mpSession->isConnected();
    if (wasClientInRoom) {
        rememberRoomForReconnect();
    } else if (m_mpIsHost) {
        m_lastRoomAddress.clear();
        m_lastRoomName.clear();
        m_lastHostName.clear();
    }
    if (m_mpSession) {
        if (m_mpActive && !m_mpIsHost)
            m_mpSession->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("leave")}});
        m_mpSession->leave();
    }
    m_mpLobbySnapshot.clear();
    m_mpActive = false;
    m_mpMatchEnded = false;
    m_mpRoundResolved = false;
    m_roundActive = false;
    if (m_timer)
        m_timer->stop();
    if (m_mpHud)
        m_mpHud->hide();
    if (m_mpResultPanel)
        m_mpResultPanel->hide();
    if (m_viewStack)
        m_viewStack->hide();
    if (m_mapButton)
        m_mapButton->hide();
    if (m_hud)
        m_hud->hide();
    if (m_topTimer)
        m_topTimer->hide();
    showStartMenu();

    // 客户端退出/掉线后回主菜单：弹「重新连接」窗口（房主不弹）。
    if (!m_mpIsHost && !m_lastRoomAddress.isEmpty()) {
        const QString notice = m_pendingLeaveNotice.isEmpty()
            ? QStringLiteral("你已离开房间")
            : m_pendingLeaveNotice;
        m_pendingLeaveNotice.clear();
        showReconnectPrompt(notice);
    }
}

int GameWindow::mpAliveCount() const
{
    int alive = 0;
    for (const MpPlayerState &state : m_mpState) {
        if (state.active && state.hp > 0)
            ++alive;
    }
    return alive;
}

void GameWindow::abortMpMatchToLobby(const QString &reason)
{
    if (!m_mpActive)
        return;
    // 房主广播：客户端也要一起退回房间，否则会卡在对局里。
    if (m_mpIsHost && m_mpSession) {
        m_mpSession->broadcast(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("match_aborted")},
            {QStringLiteral("reason"), reason},
        });
    }
    qWarning().noquote() << "对局提前结束:" << reason;

    ++m_advanceToken;   // 让还在跑的自动推进/倒计时全部失效
    hidePauseBanner();
    m_mpPaused = false;
    m_mpMatchEnded = false;
    m_mpRoundResolved = false;
    if (m_timer)
        m_timer->stop();
    if (m_countdownLabel)
        m_countdownLabel->hide();
    if (m_overlay && m_overlay->isOpen())
        m_overlay->closeImmediately();

    returnMpToLobby();

    if (m_noticeLabel && !reason.isEmpty()) {
        m_noticeLabel->setText(reason);
        m_noticeLabel->show();
        positionNotice();
        QTimer::singleShot(3600, m_noticeLabel, &QWidget::hide);
    }
}

void GameWindow::returnMpToLobby()
{
    if (!m_mpSession || !m_mpSession->isConnected())
        return;
    m_mpActive = false;
    m_mpMatchEnded = false;
    m_mpRoundResolved = false;
    m_roundActive = false;
    m_mpLocalSubmitted = false;
    m_mpState.clear();
    m_mpQuestionIds.clear();
    m_roundQuestions.clear();
    m_mpLobbySnapshot.clear();
    if (m_timer)
        m_timer->stop();
    if (m_mpHud)
        m_mpHud->hide();
    if (m_mpResultPanel)
        m_mpResultPanel->hide();
    if (m_viewStack)
        m_viewStack->hide();
    if (m_mapButton)
        m_mapButton->hide();
    if (m_hud)
        m_hud->hide();
    if (m_topTimer)
        m_topTimer->hide();
    if (m_overlay && m_overlay->isOpen())
        m_overlay->hide();
    m_mpSession->setRoomStarted(false);
    if (m_mpIsHost)
        m_mpSession->resetReadyStates();
    
    // 确保开始页可见并切换到房间大厅
    if (m_startPage)
        m_startPage->show();
    showMpLobbyPage();
}

void GameWindow::updateMpLobby()
{
    if (!m_mpSession || !m_mpLobbyPlayers || !m_mpStartButton || !m_mpReadyButton)
        return;
    const QVector<MultiplayerSession::Player> players = m_mpSession->players();
    // 大厅系统消息：加入 / 离开 / 准备状态变化。首次填充不刷屏。
    {
        const bool firstSnapshot = m_mpLobbySnapshot.isEmpty();
        QHash<int, QPair<QString, bool>> next;
        for (const MultiplayerSession::Player &player : players) {
            next.insert(player.id, qMakePair(player.name, player.ready));
            if (firstSnapshot)
                continue;
            const auto it = m_mpLobbySnapshot.constFind(player.id);
            if (it == m_mpLobbySnapshot.cend()) {
                appendChatSystem(QStringLiteral("%1 加入了房间").arg(player.name));
            } else if (!player.host && it.value().second != player.ready) {
                appendChatSystem(player.ready
                    ? QStringLiteral("%1 已准备").arg(player.name)
                    : QStringLiteral("%1 取消准备").arg(player.name));
            }
        }
        if (!firstSnapshot) {
            for (auto it = m_mpLobbySnapshot.cbegin(); it != m_mpLobbySnapshot.cend(); ++it) {
                if (!next.contains(it.key()))
                    appendChatSystem(QStringLiteral("%1 离开了房间").arg(it.value().first));
            }
        }
        m_mpLobbySnapshot = next;
    }
    if (m_mpLobbyModeCombo && m_mpLobbyRoundsSpin && m_mpLobbySecondsSpin) {
        const int modeIndex = 0;
        if (m_mpLobbyModeCombo->currentIndex() != modeIndex)
            m_mpLobbyModeCombo->setCurrentIndex(modeIndex);
        if (m_mpLobbyRoundsSpin->value() != m_mpRounds)
            m_mpLobbyRoundsSpin->setValue(m_mpRounds);
        if (m_mpLobbySecondsSpin->value() != m_mpSeconds)
            m_mpLobbySecondsSpin->setValue(m_mpSeconds);
    }
    QString playersHtml;
    bool allGuestsReady = players.size() >= 2;
    bool localReady = false;
    for (const MultiplayerSession::Player &player : players) {
        const QString marker = player.host
            ? QStringLiteral("<span style='color:#FBBF24;font-weight:800'>◆ 房主</span>")
            : player.ready
                ? QStringLiteral("<span style='color:#4ADE80;font-size:18px;font-weight:800'>✓</span>")
                : QStringLiteral("<span style='color:#64748B'>○</span>");
        // 名字用玩家自己选的颜色显示。
        const QStringList palette = MultiplayerSession::colorPalette();
        const QString nameColor = (player.colorIndex >= 0 && player.colorIndex < palette.size())
            ? palette.at(player.colorIndex)
            : QStringLiteral("#E2E8F0");
        const QString dotColor = (player.colorIndex >= 0 && player.colorIndex < palette.size())
            ? palette.at(player.colorIndex)
            : QStringLiteral("#475569");
        playersHtml += QStringLiteral(
            "<table width='100%' cellspacing='0' cellpadding='6' style='border-bottom:1px solid rgba(148,163,184,0.18)'>"
            "<tr><td><span style='color:%5;font-size:15px'>●</span> "
            "<span style='color:%1;font-weight:700'>%2</span><br/>"
            "<span style='color:#94A3B8;font-size:11px'>%3</span></td><td align='right'>%4</td></tr></table>")
            .arg(nameColor,
                 player.name.toHtmlEscaped(),
                 player.host ? QStringLiteral("房间所有者")
                             : (player.ready ? QStringLiteral("已准备") : QStringLiteral("等待准备")),
                 marker,
                 dotColor);
        if (!player.host && !player.ready)
            allGuestsReady = false;
        if (player.id == m_mpSession->localPlayerId())
            localReady = player.ready;
    }
    m_mpLobbyPlayers->setText(playersHtml);
    refreshMpColorSwatches();
    if (m_mpColorGrid)
        m_mpColorGrid->setVisible(!m_mpActive);
    m_mpStartButton->setVisible(m_mpIsHost);
    m_mpStartButton->setEnabled(m_mpIsHost && allGuestsReady);
    m_mpReadyButton->setVisible(!m_mpIsHost);
    m_mpReadyButton->setText(localReady ? QStringLiteral("取消准备") : QStringLiteral("准备"));
    m_mpReadyButton->setEnabled(!m_mpIsHost && players.size() >= 2);
    m_mpReadyButton->setStyleSheet(localReady
        ? QStringLiteral("QPushButton { background:#DC2626; color:white; border:1px solid #F87171; border-radius:8px; font-weight:800; font-size:15px; }"
                         "QPushButton:hover { background:#EF4444; } QPushButton:pressed { background:#B91C1C; }")
        : QStringLiteral("QPushButton { background:#16A34A; color:white; border:1px solid #4ADE80; border-radius:8px; font-weight:800; font-size:15px; }"
                         "QPushButton:hover { background:#22C55E; } QPushButton:pressed { background:#15803D; }"));
    if (m_mpIsHost) {
        m_mpSession->broadcast(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("room_settings")},
            {QStringLiteral("mode"), int(m_gameMode)},
            {QStringLiteral("rounds"), m_mpRounds},
            {QStringLiteral("seconds"), m_mpSeconds},
        });
    }
    if (m_mpActive) {
        QSet<int> connectedIds;
        for (const MultiplayerSession::Player &player : players)
            connectedIds.insert(player.id);
        for (MpPlayerState &state : m_mpState) {
            if (!connectedIds.contains(state.id)) {
                state.active = false;
                state.hp = 0;
                state.submitted = true;
            }
        }
        if (m_mpIsHost) {
            bool allActiveSubmitted = true;
            for (const MpPlayerState &state : m_mpState) {
                if (state.active && !state.submitted) {
                    allActiveSubmitted = false;
                    break;
                }
            }
            if (allActiveSubmitted)
                resolveMpRound();
        }
    }
    const QString modeText = QStringLiteral("2D 截图");
    const QString settingsText = QStringLiteral("%1 · %2 局 · 每局 %3 秒")
                                     .arg(modeText)
                                     .arg(m_mpRounds)
                                     .arg(m_mpSeconds);
    if (m_mpIsHost) {
        m_mpLobbyStatus->setText(players.size() < 2
                                     ? settingsText + QStringLiteral(" · 等待玩家加入…")
                                     : (allGuestsReady
                                         ? settingsText + QStringLiteral(" · 所有人已准备，可以开始")
                                         : settingsText + QStringLiteral(" · 等待所有玩家准备")));
    } else {
        m_mpLobbyStatus->setText(settingsText + (localReady
            ? QStringLiteral(" · 已准备，等待房主开始")
            : QStringLiteral(" · 点击准备后等待房主开始")));
    }
    if (m_mpActive && players.size() < 2) {
        m_mpLobbyStatus->setText(QStringLiteral("玩家离开，比赛结束"));
        m_mpActive = false;
        m_mpMatchEnded = true;
        m_mpResultPanel->hide();
        m_timer->stop();
        showMultiplayerMenu();
    }
}

void GameWindow::startMpMatch()
{
    if (!m_mpIsHost || !m_mpSession)
        return;
    const QVector<MultiplayerSession::Player> players = m_mpSession->players();
    if (players.size() < 2) {
        m_mpLobbyStatus->setText(QStringLiteral("至少需要 2 名玩家"));
        return;
    }
    for (const MultiplayerSession::Player &player : players) {
        if (!player.host && !player.ready) {
            m_mpLobbyStatus->setText(QStringLiteral("仍有玩家未准备，暂时不能开始"));
            return;
        }
    }
    if (m_mpLobbyRoundsSpin)
        m_mpRounds = m_mpLobbyRoundsSpin->value();
    if (m_mpLobbySecondsSpin)
        m_mpSeconds = m_mpLobbySecondsSpin->value();
    m_gameMode = GameMode::PhotoOnly;

    QVector<int> indices;
    for (int i = 0; i < m_pool.size(); ++i) {
        const Question &question = m_pool.at(i);
        if (m_gameMode == GameMode::PhotoOnly && question.viewType != Question::ViewType::Photo)
            continue;
        if (m_gameMode == GameMode::PanoramaOnly && question.viewType != Question::ViewType::Panorama)
            continue;
        indices.append(i);
    }
    if (indices.isEmpty()) {
        m_mpLobbyStatus->setText(QStringLiteral("该模式没有可用题目"));
        return;
    }
    for (int i = indices.size() - 1; i > 0; --i) {
        const int j = QRandomGenerator::global()->bounded(i + 1);
        std::swap(indices[i], indices[j]);
    }

    m_mpQuestionIds.clear();
    m_roundQuestions.clear();
    const int count = std::min(m_mpRounds, int(indices.size()));
    for (int i = 0; i < count; ++i) {
        const Question &question = m_pool.at(indices.at(i));
        m_mpQuestionIds.append(question.id);
        m_roundQuestions.append(question);
    }
    m_mpState.clear();
    for (const MultiplayerSession::Player &player : players) {
        MpPlayerState state;
        state.id = player.id;
        state.name = player.name;
        state.hp = GameWindow::MP_START_HP;
        state.active = true;
        m_mpState.append(state);
    }
    m_mpLocalId = m_mpSession->localPlayerId();
    m_mpActive = true;
    m_mpIsHost = true;
    m_mpMatchEnded = false;
    m_mpRoundResolved = false;
    m_mpRoundIndex = 0;
    m_mpLocalSubmitted = false;
    m_mpSession->setRoomStarted(true);

    QJsonArray questionIds;
    for (const QString &id : m_mpQuestionIds)
        questionIds.append(id);
    QJsonObject startMessage{
        {QStringLiteral("type"), QStringLiteral("match_start")},
        {QStringLiteral("mode"), int(m_gameMode)},
        {QStringLiteral("rounds"), m_mpRounds},
        {QStringLiteral("seconds"), m_mpSeconds},
        {QStringLiteral("questions"), questionIds},
    };
    m_mpSession->broadcast(startMessage);

    if (m_startBackground)
        m_startBackground->stop();
    m_startPage->hide();
    m_summaryPage->hide();
    m_viewStack->show();
    if (m_mapButton)
        m_mapButton->show();
    m_hud->show();
    m_mpHud->show();
    m_mpResultPanel->hide();
    if (m_mapButton)
        m_mapButton->raise();
    m_hud->raise();
    m_mpHud->raise();
    m_noticeLabel->raise();
    startMpRound(0);
}

void GameWindow::startMpRound(int index)
{
    if (m_countdownLabel)
        m_countdownLabel->hide();
    if (m_noticeLabel)
        m_noticeLabel->hide();
    m_mpPaused = false;
    hidePauseBanner();
    if (!m_mpActive || index < 0 || index >= m_roundQuestions.size())
        return;
    if (m_mpState.size() < 2)
        return;
    int alive = 0;
    for (const MpPlayerState &state : m_mpState)
        alive += state.hp > 0 ? 1 : 0;
    if (alive <= 1) {
        m_mpMatchEnded = true;
        QJsonObject result{
            {QStringLiteral("type"), QStringLiteral("round_result")},
            {QStringLiteral("round"), m_mpRoundIndex + 1},
            {QStringLiteral("multiplier"), m_mpMultiplier},
            {QStringLiteral("matchEnd"), true},
        };
        QJsonArray playerArray;
        for (const MpPlayerState &state : m_mpState) {
            playerArray.append(QJsonObject{
                {QStringLiteral("id"), state.id},
                {QStringLiteral("name"), state.name},
                {QStringLiteral("hp"), state.hp},
                {QStringLiteral("distance"), 0.0},
                {QStringLiteral("damage"), 0},
                {QStringLiteral("submitted"), state.submitted},
            });
        }
        result.insert(QStringLiteral("players"), playerArray);
        m_mpSession->broadcast(result);
        showMpRoundResult(result);
        return;
    }

    m_mpRoundIndex = index;
    m_mpMultiplier = 0.5 + 0.5 * index;
    qInfo().noquote() << "多人回合" << (index + 1) + 0 << "· 倍率" << m_mpMultiplier
                      << "· 存活" << [this]() {
                             int alive = 0;
                             for (const MpPlayerState &state : m_mpState)
                                 alive += state.hp > 0 ? 1 : 0;
                             return alive;
                         }();
    m_mpLocalSubmitted = false;
    m_mpRoundResolved = false;
    m_mpMatchEnded = false;
    m_mpGuesses.clear();
    for (MpPlayerState &state : m_mpState) {
        state.submitted = false;
        state.hasMarker = false;
        state.guess = QPointF(0.5, 0.5);
        state.distanceM = 0.0;
        state.score = 0;
        state.damage = 0;
        state.active = state.hp > 0;
    }
    m_remainingSeconds = m_mpSeconds;
    m_roundActive = true;
    m_mpResultPanel->hide();
    m_mpHud->show();
    updateMpHud();

    const Question &question = m_roundQuestions.at(index);
    QJsonObject roundMessage{
        {QStringLiteral("type"), QStringLiteral("round")},
        {QStringLiteral("index"), index},
        {QStringLiteral("questionId"), question.id},
        {QStringLiteral("multiplier"), m_mpMultiplier},
        {QStringLiteral("seconds"), m_mpSeconds},
    };
    m_mpSession->broadcast(roundMessage);
    loadRound(index);
}

void GameWindow::handleMpMessage(int playerId, const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();

    // 掉线暂停 / 恢复 / 弃赛（由房主广播）
    if (type == QStringLiteral("pause") && !m_mpIsHost) {
        pauseMpTimer(qint64(message.value(QStringLiteral("remainingMs")).toDouble()));
        beginPauseCountdown(QStringLiteral("%1 掉线，等待重连…")
                                .arg(message.value(QStringLiteral("name")).toString()),
                            message.value(QStringLiteral("seconds")).toInt(30));
        return;
    }
    if (type == QStringLiteral("resume") && !m_mpIsHost) {
        resumeMpTimer(qint64(message.value(QStringLiteral("remainingMs")).toDouble()));
        hidePauseBanner();
        return;
    }
    if (type == QStringLiteral("player_dropped")) {
        const int droppedId = message.value(QStringLiteral("id")).toInt();
        for (MpPlayerState &state : m_mpState) {
            if (state.id == droppedId) {
                state.active = false;
                state.hp = 0;
                state.submitted = true;
            }
        }
        updateMpHud();
        return;
    }
    if (type == QStringLiteral("ready") && m_mpIsHost && !m_mpActive) {
        m_mpSession->setPlayerReady(playerId, message.value(QStringLiteral("ready")).toBool());
        return;
    }
    if (type == QStringLiteral("chat")) {
        QString author = message.value(QStringLiteral("name")).toString();
        if (author.isEmpty() && m_mpSession) {
            for (const MultiplayerSession::Player &player : m_mpSession->players()) {
                if (player.id == playerId) {
                    author = player.name;
                    break;
                }
            }
        }
        const QString text = message.value(QStringLiteral("text")).toString().left(220);
        if (author.isEmpty())
            author = QStringLiteral("玩家");
        if (m_mpChatHistory)
            m_mpChatHistory->append(QStringLiteral("<span style='color:#A5B4FC;font-weight:700'>%1</span>　%2")
                                      .arg(author.toHtmlEscaped(), text.toHtmlEscaped()));
        if (m_mpIsHost && playerId != 0) {
            m_mpSession->broadcast(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("chat")},
                {QStringLiteral("name"), author},
                {QStringLiteral("text"), text},
            });
        }
        return;
    }
    if (type == QStringLiteral("snapshot")) {
        applyMpSnapshot(message);
        return;
    }
    if (type == QStringLiteral("room_settings") && !m_mpIsHost) {
        m_gameMode = static_cast<GameMode>(message.value(QStringLiteral("mode")).toInt());
        m_mpRounds = message.value(QStringLiteral("rounds")).toInt();
        m_mpSeconds = message.value(QStringLiteral("seconds")).toInt();
        updateMpLobby();
        return;
    }
    if (type == QStringLiteral("match_start") && !m_mpIsHost) {
        m_mpSession->setRoomStarted(true);
        m_gameMode = static_cast<GameMode>(message.value(QStringLiteral("mode")).toInt());
        m_mpRounds = message.value(QStringLiteral("rounds")).toInt();
        m_mpSeconds = message.value(QStringLiteral("seconds")).toInt();
        m_mpQuestionIds.clear();
        m_roundQuestions.clear();
        const QJsonArray questions = message.value(QStringLiteral("questions")).toArray();
        for (const QJsonValue &value : questions) {
            const QString id = value.toString();
            for (const Question &question : m_pool) {
                if (question.id == id) {
                    m_mpQuestionIds.append(id);
                    m_roundQuestions.append(question);
                    break;
                }
            }
        }
        m_mpState.clear();
        for (const MultiplayerSession::Player &player : m_mpSession->players()) {
            MpPlayerState state;
            state.id = player.id;
            state.name = player.name;
            state.hp = GameWindow::MP_START_HP;
            state.active = true;
            m_mpState.append(state);
        }
        m_mpLocalId = m_mpSession->localPlayerId();
        m_mpRoundIndex = 0;
        m_mpMatchEnded = false;
        m_mpRoundResolved = false;
        m_mpLocalSubmitted = false;
        m_mpActive = true;
        if (m_startBackground)
            m_startBackground->stop();
        m_startPage->hide();
        m_summaryPage->hide();
        m_viewStack->show();
        if (m_mapButton)
            m_mapButton->show();
        m_hud->show();
        m_mpHud->show();
        m_mpResultPanel->hide();
        if (m_mapButton)
            m_mapButton->raise();
        m_hud->raise();
        m_mpHud->raise();
        m_noticeLabel->raise();
        return;
    }
    if (!m_mpActive)
        return;
    if (type == QStringLiteral("round") && !m_mpIsHost) {
        if (m_overlay)
            m_overlay->closeImmediately();
        m_mpRoundIndex = message.value(QStringLiteral("index")).toInt();
        m_mpMultiplier = message.value(QStringLiteral("multiplier")).toDouble();
        m_mpSeconds = message.value(QStringLiteral("seconds")).toInt();
        m_mpLocalSubmitted = false;
        m_mpRoundResolved = false;
        m_mpGuesses.clear();
        for (MpPlayerState &state : m_mpState) {
            state.submitted = false;
            state.hasMarker = false;
            state.guess = QPointF(0.5, 0.5);
            state.distanceM = 0.0;
            state.score = 0;
            state.damage = 0;
            state.active = state.hp > 0;
        }
        m_mpResultPanel->hide();
        m_mpHud->show();
        updateMpHud();
        loadRound(m_mpRoundIndex);
        return;
    }
    if (type == QStringLiteral("deadline") && !m_mpIsHost) {
        const int seconds = message.value(QStringLiteral("seconds")).toInt();
        m_remainingSeconds = seconds;
        m_roundDeadlineMs = QDateTime::currentMSecsSinceEpoch() + qint64(seconds) * 1000;
        if (m_roundActive)
            m_timer->start();
        refreshHud();
        return;
    }
    if (type == QStringLiteral("round_result")) {
        showMpRoundResult(message);
        return;
    }
    if (type == QStringLiteral("match_end")) {
        showMpRoundResult(message);
        return;
    }
    if (type == QStringLiteral("match_aborted")) {
        abortMpMatchToLobby(message.value(QStringLiteral("reason")).toString());
        return;
    }
    if (type == QStringLiteral("guess") && m_mpIsHost) {
        const QJsonObject point = message.value(QStringLiteral("point")).toObject();
        handleMpGuess(playerId, QPointF(
            point.value(QStringLiteral("x")).toDouble(),
            point.value(QStringLiteral("y")).toDouble()));
        return;
    }
    if (type == QStringLiteral("pin") && m_mpIsHost) {
        const QJsonObject point = message.value(QStringLiteral("point")).toObject();
        handleMpPin(playerId, QPointF(
            point.value(QStringLiteral("x")).toDouble(),
            point.value(QStringLiteral("y")).toDouble()));
    }
}

void GameWindow::handleMpGuess(int playerId, const QPointF &guess)
{
    if (!m_mpActive || !m_mpIsHost || m_mpRoundResolved)
        return;
    for (MpPlayerState &state : m_mpState) {
        if (state.id != playerId || state.submitted)
            continue;
        state.submitted = true;
        state.hasMarker = true;
        state.guess = guess;
        if (state.active) {
            bool unsubmittedActive = false;
            for (const MpPlayerState &other : m_mpState) {
                if (other.active && !other.submitted) {
                    unsubmittedActive = true;
                    break;
                }
            }
            if (unsubmittedActive && m_remainingSeconds > 15) {
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                const qint64 capDeadline = now + 15000;
                if (m_roundDeadlineMs > capDeadline) {
                    m_roundDeadlineMs = capDeadline;
                    m_remainingSeconds = 15;
                    QJsonObject deadline{
                        {QStringLiteral("type"), QStringLiteral("deadline")},
                        {QStringLiteral("seconds"), 15},
                    };
                    m_mpSession->broadcast(deadline);
                    refreshHud();
                }
            }
        }
        break;
    }

    bool allActiveSubmitted = true;
    for (const MpPlayerState &state : m_mpState) {
        if (state.active && !state.submitted) {
            allActiveSubmitted = false;
            break;
        }
    }
    updateMpHud();
    if (allActiveSubmitted)
        resolveMpRound();
}

void GameWindow::handleMpPin(int playerId, const QPointF &guess)
{
    if (!m_mpActive || !m_mpIsHost || m_mpRoundResolved)
        return;
    for (MpPlayerState &state : m_mpState) {
        if (state.id != playerId)
            continue;
        state.hasMarker = true;
        state.guess = guess;
        break;
    }
}

void GameWindow::resolveMpRound()
{
    if (!m_mpActive || !m_mpIsHost || m_mpRoundResolved || m_mpPaused)
        return;
    const Question &question = m_roundQuestions.at(m_mpRoundIndex);
    const QPointF truth = question.normalized;
    // Max Distance is the diagonal of the rectangle formed by the street-view
    const double maxDistanceM = questionBankMaxDistanceMeters();

    for (MpPlayerState &state : m_mpState) {
        const bool answered = state.submitted || state.hasMarker;
        state.distanceM = answered
            ? Score::distanceMeters(state.guess, truth)
            : maxDistanceM;
        state.score = answered
            ? Score::scoreForDistanceMeters(state.distanceM, maxDistanceM)
            : 0;
    }

    QVector<int> active;
    for (int i = 0; i < m_mpState.size(); ++i) {
        if (m_mpState.at(i).active)
            active.append(i);
    }
    std::sort(active.begin(), active.end(), [this](int a, int b) {
        return m_mpState.at(a).score > m_mpState.at(b).score;
    });
    for (int pos = 0; pos < active.size(); ++pos) {
        const int index = active.at(pos);
        double damage = 0.0;
        if (pos > 0) {
            double sum = 0.0;
            for (int j = 0; j < pos; ++j)
                // Better (larger) round score causes damage to scale down;
                // worse (smaller) round score receives progressively more.
                sum += m_mpState.at(active.at(j)).score - m_mpState.at(index).score;
            damage = m_mpMultiplier * sum / pos;
        }
        m_mpState[index].damage = qRound(damage);
        m_mpState[index].hp = std::max(0, m_mpState[index].hp - m_mpState[index].damage);
    }
    for (MpPlayerState &state : m_mpState) {
        if (!state.active)
            state.damage = 0;
        state.active = state.hp > 0;
    }
    for (const MpPlayerState &state : m_mpState) {
        qInfo().noquote() << "  结算" << state.name
                          << "得分" << state.score
                          << "距离" << qRound(state.distanceM) << "m"
                          << "扣血" << state.damage
                          << "剩余 HP" << state.hp;
    }

    QVector<int> ranking;
    for (int i = 0; i < m_mpState.size(); ++i)
        ranking.append(i);
    std::sort(ranking.begin(), ranking.end(), [this](int a, int b) {
        return m_mpState.at(a).score > m_mpState.at(b).score;
    });

    const int alive = std::count_if(m_mpState.cbegin(), m_mpState.cend(),
                                    [](const MpPlayerState &state) { return state.hp > 0; });
    // 比赛是否结束延后到"下一回合开始前"再判定：本回合仍然完整播放地图回放与得分动画。
    const bool finalRound = alive <= 1 || m_mpRoundIndex + 1 >= m_mpRounds;
    const bool matchEnd = false;
    QJsonArray playerArray;
    for (int index : ranking) {
        const MpPlayerState &state = m_mpState.at(index);
        playerArray.append(QJsonObject{
            {QStringLiteral("id"), state.id},
            {QStringLiteral("name"), state.name},
            {QStringLiteral("hp"), state.hp},
            {QStringLiteral("distance"), state.distanceM},
            {QStringLiteral("score"), state.score},
            {QStringLiteral("damage"), state.damage},
            {QStringLiteral("guessX"), state.guess.x()},
            {QStringLiteral("guessY"), state.guess.y()},
            {QStringLiteral("submitted"), state.submitted},
            {QStringLiteral("hasMarker"), state.hasMarker},
            {QStringLiteral("alive"), state.hp > 0},
        });
    }
    QJsonObject result{
        {QStringLiteral("type"), QStringLiteral("round_result")},
        {QStringLiteral("round"), m_mpRoundIndex + 1},
        {QStringLiteral("multiplier"), m_mpMultiplier},
        {QStringLiteral("matchEnd"), matchEnd},
        {QStringLiteral("finalRound"), finalRound},        {QStringLiteral("truthX"), truth.x()},
        {QStringLiteral("truthY"), truth.y()},
        {QStringLiteral("players"), playerArray},
    };
    m_mpSession->broadcast(result);
    showMpRoundResult(result);
}

void GameWindow::showMpRoundResult(const QJsonObject &result)
{
    m_mpRoundResolved = true;
    m_roundActive = false;
    m_timer->stop();
    updateAlarmOverlay();
    if (m_topTimer)
        m_topTimer->hide();
    const bool matchEnd = result.value(QStringLiteral("matchEnd")).toBool();
    const bool finalRound = result.value(QStringLiteral("finalRound")).toBool();
    qInfo().noquote() << "回合结算" << result.value(QStringLiteral("round")).toInt()
                      << "· 倍率" << result.value(QStringLiteral("multiplier")).toDouble()
                      << "· 最终局" << (finalRound ? "是" : "否")
                      << "· 比赛结束" << (matchEnd ? "是" : "否");
    m_mpMatchEnded = matchEnd;
    if (matchEnd) {
        if (m_overlay)
            m_overlay->closeImmediately();
        QTimer::singleShot(8000, this, [this]() {
            if (m_mpActive && m_mpMatchEnded)
                returnMpToLobby();
        });
    }
    const int round = result.value(QStringLiteral("round")).toInt();
    const double multiplier = result.value(QStringLiteral("multiplier")).toDouble();

    QVector<QJsonObject> rows;
    for (const QJsonValue &value : result.value(QStringLiteral("players")).toArray())
        rows.append(value.toObject());
    for (const QJsonObject &row : rows) {
        const int id = row.value(QStringLiteral("id")).toInt();
        for (MpPlayerState &state : m_mpState) {
            if (state.id != id)
                continue;
            state.hp = row.value(QStringLiteral("hp")).toInt();
            state.distanceM = row.value(QStringLiteral("distance")).toDouble();
            state.score = row.value(QStringLiteral("score")).toInt();
            state.damage = row.value(QStringLiteral("damage")).toInt();
            state.submitted = row.value(QStringLiteral("submitted")).toBool();
            state.hasMarker = row.value(QStringLiteral("hasMarker")).toBool();
            state.active = state.hp > 0;
            break;
        }
    }

    const MpPlayerState *localState = nullptr;
    for (const MpPlayerState &state : m_mpState) {
        if (state.id == m_mpLocalId) {
            localState = &state;
            break;
        }
    }

    // 座位顺序 = 顶部血条顺序（自己第一，其余按血量降序）。分数卡、标点颜色都跟着这个
    // 顺序走，这样"每张卡就摆在本人血条正下方"、"每个人的标点和头像同色"。
    QVector<HudFighter> hudFighters = mpHudFighters();

    QPointF localGuess(0.5, 0.5);
    QVector<QPointF> otherGuesses;
    QString localDistanceLabel;
    QVector<QString> otherDistanceLabels;
    QVector<QColor> otherPinColors;
    QVector<QString> scoreNames;
    QVector<int> scoreValues;
    QVector<int> damageValues;
    QVector<QColor> seatColors;
    int localIndex = 0;
    int localScore = 0;
    bool localHasGuess = false;
    QColor localSeatColor = BattleHudWidget::seatColor(0, true);
    // 每个玩家的数据以房主广播的 rows 为准（客户端的 m_mpState 里没有别人的标点/距离）。
    QHash<QString, QJsonObject> rowsByName;
    for (const QJsonObject &row : rows)
        rowsByName.insert(row.value(QStringLiteral("name")).toString(), row);
    for (int seat = 0; seat < hudFighters.size(); ++seat) {
        const HudFighter &fighter = hudFighters.at(seat);
        const QColor seatColor = BattleHudWidget::fighterColor(fighter, seat);
        const QJsonObject row = rowsByName.value(fighter.name);
        const int score = row.value(QStringLiteral("score")).toInt();
        const int damage = row.value(QStringLiteral("damage")).toInt();
        const double distanceM = row.value(QStringLiteral("distance")).toDouble();
        const bool hasGuess = row.value(QStringLiteral("submitted")).toBool()
            || row.value(QStringLiteral("hasMarker")).toBool();
        const QPointF guess(row.value(QStringLiteral("guessX")).toDouble(0.5),
                            row.value(QStringLiteral("guessY")).toDouble(0.5));
        scoreNames.append(fighter.name);
        scoreValues.append(score);
        damageValues.append(damage);
        seatColors.append(seatColor);
        if (fighter.local) {
            localIndex = seat;
            localScore = score;
            localSeatColor = seatColor;
            localHasGuess = hasGuess;
            if (hasGuess) {
                localGuess = guess;
                localDistanceLabel = QStringLiteral("%1 m").arg(distanceM, 0, 'f', 0);
            }
        } else if (hasGuess) {
            otherGuesses.append(guess);
            otherDistanceLabels.append(QStringLiteral("%1 m").arg(distanceM, 0, 'f', 0));
            otherPinColors.append(seatColor);
        }
    }
    std::sort(rows.begin(), rows.end(), [matchEnd](const QJsonObject &a, const QJsonObject &b) {
        if (matchEnd)
            return a.value(QStringLiteral("hp")).toInt() > b.value(QStringLiteral("hp")).toInt();
        return a.value(QStringLiteral("score")).toInt()
             > b.value(QStringLiteral("score")).toInt();
    });

    QString html = QStringLiteral("<div style='font-size:26px;font-weight:800;color:#eefaff'>%1</div>")
                       .arg(matchEnd ? QStringLiteral("比赛结束")
                                     : QStringLiteral("第 %1 回合").arg(round));
    // 比赛结束面板只看最终 HP，不再显示单回合的得分/距离/扣血。
    html += matchEnd
        ? QStringLiteral("<div style='color:#8ba0ad;margin-bottom:10px'>最终排名 · 剩余 HP</div>")
        : QStringLiteral("<div style='color:#8ba0ad;margin-bottom:10px'>倍率 ×%1</div>")
              .arg(static_cast<double>(multiplier), 0, 'f', 1);
    int rank = 1;
    for (const QJsonObject &row : rows) {
        const int hp = row.value(QStringLiteral("hp")).toInt();
        const double distance = row.value(QStringLiteral("distance")).toDouble();
        const int score = row.value(QStringLiteral("score")).toInt();
        const int damage = row.value(QStringLiteral("damage")).toInt();
        const bool submitted = row.value(QStringLiteral("submitted")).toBool();
        const bool hasMarker = row.value(QStringLiteral("hasMarker")).toBool();
        const QString name = row.value(QStringLiteral("name")).toString();
        const QString color = hp > 0 ? QStringLiteral("#dceaf0") : QStringLiteral("#ff7b72");
        if (matchEnd) {
            const QString rankColor = (rank == 1 && hp > 0)
                ? QStringLiteral("#FACC15")
                : (hp > 0 ? QStringLiteral("#dceaf0") : QStringLiteral("#ff7b72"));
            html += QStringLiteral(
                        "<div style='color:%1;margin:5px 0;font-size:18px'>"
                        "#%2 %3　<b>%4</b></div>")
                        .arg(rankColor)
                        .arg(rank++)
                        .arg(name)
                        .arg(hp > 0 ? QStringLiteral("HP %1").arg(hp)
                                    : QStringLiteral("已淘汰"));
            continue;
        }
        const QString distanceText = (submitted || hasMarker)
            ? QStringLiteral("%1 m").arg(distance, 0, 'f', 0)
            : QStringLiteral("未作答");
        html += QStringLiteral(
                    "<div style='color:%1;margin:3px 0'>#%2 %3　得分 <b>%4</b>　距离 %5　扣血 %6　HP %7</div>")
                    .arg(color)
                    .arg(rank++)
                    .arg(name)
                    .arg(score)
                    .arg(distanceText)
                    .arg(damage)
                    .arg(hp);
    }
    m_mpResultLabel->setText(html);
    m_mpContinueButton->setText(matchEnd ? QStringLiteral("返回房间")
                                        : QStringLiteral("下一回合即将开始…"));
    m_mpContinueButton->setVisible(matchEnd);
    m_mpContinueButton->setEnabled(matchEnd);
    m_mpResultPanel->setGeometry(rect());
    // ---- 顶部血条：先把每个人的血量/扣血算好，座位顺序由 mpHudFighters() 固定 ----
    QVector<qreal> hudTargets;   // 与 scoreNames 对齐：每张分数卡对应的血条格中心（窗口坐标）
    qreal hudTargetY = 74.0;
    QVector<QRect> cardRects;    // 每张分数卡的位置：本人血条正下方
    if (m_mpHud && !matchEnd && !hudFighters.isEmpty()) {
        const int maxRoundScore = [&rows]() {
            int best = 0;
            for (const QJsonObject &row : rows)
                best = qMax(best, row.value(QStringLiteral("score")).toInt());
            return best;
        }();
        for (HudFighter &fighter : hudFighters) {
            for (const MpPlayerState &state : m_mpState) {
                if (state.name != fighter.name)
                    continue;
                // 并列最高分不掉血：起点 = 终点，血条完全不动。
                fighter.startHp = (state.score == maxRoundScore)
                    ? state.hp
                    : qMin(GameWindow::MP_START_HP, state.hp + state.damage);
                fighter.endHp = state.hp;
                break;
            }
        }

        updateMpHud();  // 先定好宽度/位置
        QVector<HudFighter> before = hudFighters;
        for (HudFighter &fighter : before)
            fighter.hp = fighter.startHp;
        m_mpHud->setFighters(before, multiplier);
        m_mpHud->show();
        m_mpHud->raise();

        const QVector<qreal> centers = m_mpHud->cellCentersX();
        const QVector<QRectF> bars = m_mpHud->barRects();
        hudTargetY = m_mpHud->y() + 32.0;
        for (int i = 0; i < bars.size() && i < scoreNames.size(); ++i) {
            // 分数卡：贴着本人血条正下方（留出名字那一行），宽度与血条一致。
            cardRects.append(QRect(qRound(m_mpHud->x() + bars.at(i).left()),
                                   qRound(m_mpHud->y() + bars.at(i).bottom()) + 22,
                                   qRound(bars.at(i).width()), 66));
        }
        for (const QString &name : scoreNames) {
            qreal targetX = m_mpHud->x() + m_mpHud->width() * 0.5;
            for (int i = 0; i < hudFighters.size() && i < centers.size(); ++i) {
                if (hudFighters.at(i).name == name) {
                    targetX = m_mpHud->x() + centers.at(i);
                    break;
                }
            }
            hudTargets.append(targetX);
        }

        // 扣血数字落到血条时（damagePhaseStarted）才开始掉血。
        connect(m_overlay, &MapOverlay::damagePhaseStarted, this,
                [this, hudFighters, multiplier]() {
            if (m_mpHud)
                m_mpHud->animateFighters(hudFighters, multiplier);
        }, Qt::SingleShotConnection);
    }

    if (matchEnd) {
        m_mpResultPanel->show();
        m_mpResultPanel->raise();
    } else {
        m_mpResultPanel->hide();
        if (m_overlay) {
            const QPointF truth(result.value(QStringLiteral("truthX")).toDouble(0.5),
                                result.value(QStringLiteral("truthY")).toDouble(0.5));
            MapOverlay::ReviewStyle style;
            style.damageTargetX = hudTargets;
            style.damageTargetY = hudTargetY;
            style.cardRects = cardRects;
            style.seatColors = seatColors;
            style.localSeatColor = localSeatColor;
            style.otherPinColors = otherPinColors;
            m_overlay->openForMultiplayerReview(mapButtonRectInWindow(), localGuess,
                                                otherGuesses, truth, localScore,
                                                localDistanceLabel, otherDistanceLabels,
                                                scoreNames, scoreValues, damageValues, localIndex,
                                                finalRound, localHasGuess, style);
            // 结算地图铺满屏幕，血条要盖在地图上面才看得见。
            if (m_mpHud) {
                m_mpHud->show();
                m_mpHud->raise();
            }
        }
    }

    if (m_mpHud && matchEnd) {
        // 最终排名面板：HP 已在回放阶段扣完，这里只刷新静态数值。
        updateMpHud();
        m_mpHud->show();
        m_mpHud->raise();
    }

    if (localState && localState->damage > 0 && m_noticeLabel) {
        // 伤害提示延后到分数结算阶段，避免提前剧透扣血数值。
        const int localRoundScore = localState->score;
        const int localDamage = localState->damage;
        QTimer::singleShot(4000, this, [this, round, localRoundScore, localDamage]() {
            if (!m_mpActive || !m_noticeLabel)
                return;
            m_noticeLabel->setText(QStringLiteral("第 %1 回合结算　得分 %2　受到 %3 HP")
                                       .arg(round)
                                       .arg(localRoundScore)
                                       .arg(localDamage));
            m_noticeLabel->show();
            positionNotice();
            // 不自动消失：这条要一直留到倒计时结束（下一局开始）再收。
        });
    }

    // 回放结束后（6.2 秒）进入倒计时或直接结算：
    // 非最后一局：6.2s 起 3-2-1 倒计时，9.2s 开下一局；
    // 最后一局：不播倒计时，9.2s 直接弹最终结算。
    if (!matchEnd) {
        if (finalRound) {
            if (m_mpIsHost) {
                QTimer::singleShot(9200, this, [this]() {
                    if (m_mpActive && m_mpRoundResolved && !m_mpMatchEnded)
                        finishMpMatch();
                });
            }
        } else {
            QTimer::singleShot(6200, this, [this]() { showRoundCountdown(); });
            if (m_mpIsHost) {
                QTimer::singleShot(9200, this, [this]() {
                    if (!m_mpActive || !m_mpRoundResolved || m_mpMatchEnded)
                        return;
                    if (m_overlay)
                        m_overlay->closeImmediately();
                    startMpRound(m_mpRoundIndex + 1);
                });
            }
        }
    }
}

void GameWindow::showCountdownNumber(int number)
{
    if (!m_countdownLabel)
        return;
    Sfx::play(QStringLiteral("countdown_tick"), 0.8);
    m_countdownLabel->setText(QString::number(number));
    m_countdownLabel->show();
    m_countdownLabel->raise();

    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(m_countdownLabel->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(m_countdownLabel);
        m_countdownLabel->setGraphicsEffect(effect);
    }
    effect->setOpacity(1.0);
    auto *fade = new QPropertyAnimation(effect, "opacity", this);
    fade->setDuration(880);
    fade->setStartValue(1.0);
    fade->setEndValue(0.18);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void GameWindow::applyMpSnapshot(const QJsonObject &message)
{
    if (!m_mpSession)
        return;

    m_mpLocalId = m_mpSession->localPlayerId();
    m_mpIsHost = false;
    m_mpActive = true;
    m_mpSession->setRoomStarted(true);
    m_mpMatchEnded = false;
    m_mpRoundResolved = false;
    m_mpLocalSubmitted = false;
    m_mpMultiplier = message.value(QStringLiteral("multiplier")).toDouble();
    const int snapshotSeconds = message.value(QStringLiteral("seconds")).toInt();
    if (snapshotSeconds > 0)
        m_mpSeconds = snapshotSeconds;

    m_roundQuestions.clear();
    m_mpQuestionIds.clear();
    for (const QJsonValue &value : message.value(QStringLiteral("questions")).toArray()) {
        const QString id = value.toString();
        for (const Question &question : m_pool) {
            if (question.id == id) {
                m_roundQuestions.append(question);
                m_mpQuestionIds.append(id);
                break;
            }
        }
    }
    const int roundIndex = message.value(QStringLiteral("round")).toInt();
    if (m_roundQuestions.isEmpty() || roundIndex < 0 || roundIndex >= m_roundQuestions.size()) {
        qWarning().noquote() << "重连快照无效，返回大厅";
        return;
    }

    // 已经在同一回合里（只是网络抖动）：只同步数值，不重载题目。
    const bool sameRound = m_mpActive && roundIndex == m_mpRoundIndex
        && m_roundQuestions.at(roundIndex).id
               == message.value(QStringLiteral("questionId")).toString();
    const QJsonArray playerRows = message.value(QStringLiteral("players")).toArray();
    if (sameRound) {
        for (const QJsonValue &value : playerRows) {
            const QJsonObject row = value.toObject();
            for (MpPlayerState &state : m_mpState) {
                if (state.id != row.value(QStringLiteral("id")).toInt())
                    continue;
                state.hp = row.value(QStringLiteral("hp")).toInt();
                state.score = row.value(QStringLiteral("score")).toInt();
                state.submitted = row.value(QStringLiteral("submitted")).toBool();
                state.hasMarker = row.value(QStringLiteral("hasMarker")).toBool();
                state.active = state.hp > 0;
                break;
            }
        }
        resumeMpTimer(qint64(message.value(QStringLiteral("remainingMs")).toDouble()));
        hidePauseBanner();
        updateMpHud();
        qInfo().noquote() << "重连快照（同回合）已同步，剩余" << m_remainingSeconds << "秒";
        return;
    }

    m_mpState.clear();
    for (const QJsonValue &value : playerRows) {
        const QJsonObject row = value.toObject();
        MpPlayerState state;
        state.id = row.value(QStringLiteral("id")).toInt();
        state.name = row.value(QStringLiteral("name")).toString();
        state.hp = row.value(QStringLiteral("hp")).toInt();
        state.score = row.value(QStringLiteral("score")).toInt();
        state.damage = row.value(QStringLiteral("damage")).toInt();
        state.guess = QPointF(row.value(QStringLiteral("guessX")).toDouble(0.5),
                              row.value(QStringLiteral("guessY")).toDouble(0.5));
        state.submitted = row.value(QStringLiteral("submitted")).toBool();
        state.hasMarker = row.value(QStringLiteral("hasMarker")).toBool();
        state.active = state.hp > 0;
        m_mpState.append(state);
    }

    m_startPage->hide();
    m_summaryPage->hide();
    m_viewStack->show();
    m_hud->show();
    m_mpHud->show();
    m_mpResultPanel->hide();
    if (m_mapButton)
        m_mapButton->show();

    loadRound(roundIndex);
    resumeMpTimer(qint64(message.value(QStringLiteral("remainingMs")).toDouble()));
    hidePauseBanner();
    qInfo().noquote() << "应用重连快照: 回合" << (roundIndex + 1)
                      << "/" << m_roundQuestions.size()
                      << "· 剩余" << m_remainingSeconds << "秒";
}

void GameWindow::pauseMpTimer(qint64 remainingMs){
    m_mpPaused = true;
    m_pausedRemainingMs = qMax<qint64>(0, remainingMs);
    // 顶部计时按暂停时刻的数字冻住（不再往下走）。
    m_remainingSeconds = int((m_pausedRemainingMs + 999) / 1000);
    if (m_timer)
        m_timer->stop();
    refreshHud();
}

void GameWindow::resumeMpTimer(qint64 remainingMs)
{
    m_mpPaused = false;
    m_pausedRemainingMs = qMax<qint64>(0, remainingMs);
    m_remainingSeconds = int((m_pausedRemainingMs + 999) / 1000);
    m_roundDeadlineMs = QDateTime::currentMSecsSinceEpoch() + m_pausedRemainingMs;
    if (m_roundActive && (m_mpActive || m_timerEnabled) && m_timer)
        m_timer->start();
    refreshHud();
}

void GameWindow::showPauseBanner(const QString &text)
{
    if (!m_pauseLabel)
        return;
    m_pauseLabel->setText(text);
    m_pauseLabel->setGeometry(rect());
    m_pauseLabel->show();
    m_pauseLabel->raise();
    // 遮罩每秒重刷一次，会把「结束等待」按钮压下去，这里补一次 raise。
    if (m_pauseSkipButton && m_pauseSkipButton->isVisible())
        m_pauseSkipButton->raise();
}

void GameWindow::hidePauseBanner()
{
    if (m_pauseTimer)
        m_pauseTimer->stop();
    m_pauseSeconds = 0;
    if (m_pauseSkipButton)
        m_pauseSkipButton->hide();
    if (m_pauseLabel)
        m_pauseLabel->hide();
}

// 判掉线玩家弃赛 → 只剩一人就结束对局，否则恢复计时继续打。
// 由「30 秒到点」或房主点「结束等待」触发。
void GameWindow::finishMpPauseWait()
{
    if (!m_pauseLabel)
        return;
    if (m_pauseTimer)
        m_pauseTimer->stop();
    m_pauseSeconds = 0;

    // 兜底：即使网络层还没回 peerDropped，也把掉线的人判掉，避免卡死。
    if (m_mpIsHost && m_mpSession) {
        for (const MultiplayerSession::Player &player : m_mpSession->players()) {
            if (player.host || player.connected)
                continue;
            for (MpPlayerState &state : m_mpState) {
                if (state.id == player.id) {
                    state.active = false;
                    state.hp = 0;
                    state.submitted = true;
                }
            }
            m_mpSession->broadcast(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("player_dropped")},
                {QStringLiteral("id"), player.id},
            });
        }
    }

    const qint64 remaining = qMax<qint64>(1, m_pausedRemainingMs);
    // 只剩一人：不再继续对局，直接结束并退回房间大厅。
    if (m_mpIsHost && mpAliveCount() <= 1) {
        m_mpPaused = false;
        abortMpMatchToLobby(QStringLiteral("房间里只剩你一人，对局结束"));
        return;
    }
    if (m_mpIsHost && m_mpSession) {
        m_mpSession->broadcast(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("resume")},
            {QStringLiteral("remainingMs"), double(remaining)},
        });
    }
    resumeMpTimer(remaining);
    hidePauseBanner();
    updateMpHud();
    qWarning().noquote() << "结束掉线等待，继续对局（剩余" << remaining << "ms）";
}

void GameWindow::beginPauseCountdown(const QString &prefix, int seconds)
{
    if (!m_pauseLabel)
        return;
    m_pausePrefix = prefix;
    m_pauseSeconds = qMax(0, seconds);

    auto tick = [this]() {
        if (m_pauseSeconds <= 0) {
            finishMpPauseWait();
            return;
        }
        showPauseBanner(QStringLiteral("%1（%2 秒）").arg(m_pausePrefix).arg(m_pauseSeconds));
        --m_pauseSeconds;
    };

    if (!m_pauseTimer) {
        m_pauseTimer = new QTimer(this);
        m_pauseTimer->setInterval(1000);
        connect(m_pauseTimer, &QTimer::timeout, this, tick);
    }
    m_pauseTimer->stop();
    tick();
    if (m_pauseSeconds > 0)
        m_pauseTimer->start();

    // 房主可以不等满 30 秒：直接把掉线玩家判弃赛。
    if (m_pauseSkipButton) {
        if (m_mpIsHost && m_pauseSeconds > 0) {
            m_pauseSkipButton->adjustSize();
            m_pauseSkipButton->move((width() - m_pauseSkipButton->width()) / 2,
                                    height() / 2 + 90);
            m_pauseSkipButton->show();
            m_pauseSkipButton->raise();
        } else {
            m_pauseSkipButton->hide();
        }
    }
}

void GameWindow::showRoundCountdown()
{
    if (!m_countdownLabel)
        return;
    // 回合结算卡片在倒计时期间保持显示，等下一局开始（关地图）时再一起收起。
    const int box = 260;
    m_countdownLabel->setGeometry((width() - box) / 2, (height() - box) / 2, box, box);
    for (int i = 0; i < 3; ++i) {
        QTimer::singleShot(i * 1000, this, [this, i]() { showCountdownNumber(3 - i); });
    }
    QTimer::singleShot(3000, this, [this]() {
        if (m_countdownLabel)
            m_countdownLabel->hide();
        // 倒计时结束 = 本回合彻底翻篇，顶部的回合结算横条也收掉。
        if (m_noticeLabel)
            m_noticeLabel->hide();
        Sfx::play(QStringLiteral("countdown_go"));
    });
}

void GameWindow::finishMpMatch()
{
    if (!m_mpActive || !m_mpIsHost)
        return;
    m_mpMatchEnded = true;
    Sfx::play(QStringLiteral("match_end"), 0.9);

    QJsonArray playerArray;
    for (const MpPlayerState &state : m_mpState) {
        playerArray.append(QJsonObject{
            {QStringLiteral("id"), state.id},
            {QStringLiteral("name"), state.name},
            {QStringLiteral("hp"), state.hp},
            {QStringLiteral("distance"), state.distanceM},
            {QStringLiteral("score"), state.score},
            {QStringLiteral("damage"), state.damage},
            {QStringLiteral("guessX"), state.guess.x()},
            {QStringLiteral("guessY"), state.guess.y()},
            {QStringLiteral("submitted"), state.submitted},
            {QStringLiteral("hasMarker"), state.hasMarker},
            {QStringLiteral("alive"), state.hp > 0},
        });
    }
    const QJsonObject result{
        {QStringLiteral("type"), QStringLiteral("match_end")},
        {QStringLiteral("round"), m_mpRoundIndex + 1},
        {QStringLiteral("multiplier"), m_mpMultiplier},
        {QStringLiteral("matchEnd"), true},
        {QStringLiteral("finalRound"), true},
        {QStringLiteral("players"), playerArray},
    };
    m_mpSession->broadcast(result);
    showMpRoundResult(result);
}

void GameWindow::chooseMpColor(int index)
{
    if (!m_mpSession)
        return;
    const QStringList palette = MultiplayerSession::colorPalette();
    if (index < 0 || index >= palette.size())
        return;
    if (m_mpIsHost)
        m_mpSession->setPlayerColor(m_mpSession->localPlayerId(), index);
    else
        m_mpSession->send(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("color")},
            {QStringLiteral("index"), index},
        });
}

void GameWindow::refreshMpColorSwatches()
{
    if (m_mpColorSwatches.isEmpty() || !m_mpSession)
        return;
    const QStringList palette = MultiplayerSession::colorPalette();
    QSet<int> taken;
    int mine = -1;
    for (const MultiplayerSession::Player &player : m_mpSession->players()) {
        if (player.colorIndex < 0)
            continue;
        taken.insert(player.colorIndex);
        if (player.id == m_mpSession->localPlayerId())
            mine = player.colorIndex;
    }
    for (int i = 0; i < m_mpColorSwatches.size(); ++i) {
        QPushButton *swatch = m_mpColorSwatches.at(i);
        const bool isMine = (i == mine);
        const bool isTaken = !isMine && taken.contains(i);
        const QString hex = palette.value(i, QStringLiteral("#475569"));
        QColor color(hex);
        if (isTaken)
            color.setAlpha(50);      // 别人占用的颜色压暗
        swatch->setEnabled(!isTaken);
        swatch->setText(isTaken ? QStringLiteral("×") : QString());
        swatch->setStyleSheet(
            QStringLiteral("QPushButton { background: rgba(%1,%2,%3,%4);"
                           " border: %5px solid %6; border-radius: 6px; }"
                           "QPushButton:hover { border-color: #FFFFFF; }")
                .arg(color.red())
                .arg(color.green())
                .arg(color.blue())
                .arg(color.alpha())
                .arg(isMine ? 3 : 1)
                .arg(isMine ? QStringLiteral("#FFFFFF")
                            : QStringLiteral("rgba(255,255,255,70)")));
    }
}

void GameWindow::updateMpHud()
{
    if (!m_mpHud)
        return;

    m_mpHud->setFighters(mpHudFighters(), m_mpMultiplier);
    const int hudWidth = qMin(1180, qMax(680, width() - 80));
    m_mpHud->setFixedWidth(hudWidth);
    m_mpHud->move((width() - hudWidth) / 2, 64);
    // 选点地图打开时血条要待在地图下面（提交答案会刷新血条，不能顺手抢到最上层）；
    // 结算回放时相反，血条要在结算地图上面。
    const bool pickOverlayOpen = m_overlay && m_overlay->isOpen() && !m_overlay->isMultiplayerReview();
    if (!pickOverlayOpen)
        m_mpHud->raise();
}

// 顺序固定：自己排第一，其余按剩余血量从高到低（并列时按 playerId）。
// 座位号在「静态刷新」和「结算动画」两处必须一致，否则扣血数字会飞错格子。
QVector<GameWindow::HudFighter> GameWindow::mpHudFighters() const
{
    QVector<HudFighter> fighters;
    // 座位顺序固定成"加入房间的顺序"（playerId 升序）：
    // 这样每个人屏幕上看到的排列完全一致，不会各看各的。
    QVector<const MpPlayerState *> ordered;
    for (const MpPlayerState &state : m_mpState)
        ordered.append(&state);
    std::sort(ordered.begin(), ordered.end(),
              [](const MpPlayerState *a, const MpPlayerState *b) { return a->id < b->id; });

    for (const MpPlayerState *state : ordered) {
        HudFighter fighter;
        fighter.name = state->name;
        fighter.hp = state->hp;
        fighter.startHp = state->hp;
        fighter.endHp = state->hp;
        fighter.local = (state->id == m_mpLocalId);
        fighter.alive = state->hp > 0;
        fighters.append(fighter);
    }

    // 把房间里各自选的颜色带上（血条头像 / 标点 / 分数卡统一用这个颜色）。
    if (m_mpSession) {
        const QStringList palette = MultiplayerSession::colorPalette();
        for (const MultiplayerSession::Player &player : m_mpSession->players()) {
            if (player.colorIndex < 0 || player.colorIndex >= palette.size())
                continue;
            for (HudFighter &fighter : fighters) {
                if (fighter.name == player.name)
                    fighter.color = palette.at(player.colorIndex);
            }
        }
    }
    return fighters;
}

void GameWindow::showWithSavedState()
{
    QSettings &settings = *PortableSettings::appSettings();
    const bool fullscreen = settings.value(QStringLiteral("game/fullscreen"), true).toBool();
    const QByteArray saved = settings.value(QStringLiteral("game/windowGeometry")).toByteArray();
    if (fullscreen) {
        showFullScreen();
        return;
    }
    if (!saved.isEmpty() && restoreGeometry(saved)) {
        show();
        return;
    }
    resize(1440, 900);
    show();
}

void GameWindow::closeEvent(QCloseEvent *event)
{
    QSettings &settings = *PortableSettings::appSettings();
    if (!isFullScreen())
        settings.setValue(QStringLiteral("game/windowGeometry"), saveGeometry());
    settings.setValue(QStringLiteral("game/fullscreen"), isFullScreen());
    settings.sync();
    QWidget::closeEvent(event);
}

void GameWindow::showStartMenu()
{
    ++m_advanceToken;
    hideReconnectPrompt();
    updateAlarmOverlay();
    m_mpPaused = false;
    hidePauseBanner();
    m_timer->stop();
    m_roundActive = false;
    m_timerPausedForOverlay = false;
    m_overlay->hide();
    m_summaryPage->hide();
    if (m_viewStack)
        m_viewStack->hide();
    m_hud->hide();
    if (m_topTimer)
        m_topTimer->hide();
    if (m_mpHud)
        m_mpHud->hide();
    if (m_mpResultPanel)
        m_mpResultPanel->hide();
    m_noticeLabel->hide();
    if (m_countdownLabel)
        m_countdownLabel->hide();
    if (m_startStack && m_mainMenuPage)
        m_startStack->setCurrentWidget(m_mainMenuPage);
    if (m_modeHintLabel)
        m_modeHintLabel->hide();
    m_startPage->setGeometry(rect());
    if (m_startBackground) {
        m_startBackground->setGeometry(m_startPage->rect());
        m_startBackground->setDim(0.45);
        m_startBackground->lower();
        m_startBackground->start();
    }
    m_startPage->show();
    m_startPage->raise();
    m_startPage->setFocus();
}

void GameWindow::startNewGame()
{
    if (m_startBackground)
        m_startBackground->stop();
    m_startPage->hide();
    m_summaryPage->hide();
    m_viewStack->show();
    if (m_mapButton)
        m_mapButton->show();
    m_hud->show();
    if (m_mapButton)
        m_mapButton->raise();
    m_hud->raise();
    m_noticeLabel->raise();
    m_submitButton->setEnabled(true);
    m_submitButton->setText(QStringLiteral("提交答案"));

    m_results.clear();
    m_roundIndex = 0;
    m_hasGuess = false;
    m_guess = QPointF(0.5, 0.5);
    m_roundActive = false;
    m_timerPausedForOverlay = false;

    if (m_pool.isEmpty())
        buildPlaceholderBank();

    selectRoundQuestions();
    if (m_roundQuestions.isEmpty()) {
        showStartMenu();
        if (m_startStack && m_singlePlayerPage)
            m_startStack->setCurrentWidget(m_singlePlayerPage);
        if (m_modeHintLabel) {
            m_modeHintLabel->setText(
                QStringLiteral("当前没有可用的截图题目，请先在题库中添加 2D 题目。"));
            m_modeHintLabel->show();
        }
        return;
    }
    updateNotice();

    loadRound(0);
}

void GameWindow::loadRound(int index)
{
    ++m_advanceToken;
    if (index < 0 || index >= m_roundQuestions.size())
        return;

    m_roundIndex = index;
    m_hasGuess = false;
    m_guess = QPointF(0.5, 0.5);
    if (m_overlay)
        m_overlay->resetMapViewForNewQuestion();
    if (m_mpActive)
        m_mpRoundIndex = index;
    const int roundSeconds = m_mpActive ? m_mpSeconds : m_secondsPerRound;
    m_remainingSeconds = roundSeconds;
    m_roundDeadlineMs = QDateTime::currentMSecsSinceEpoch() + qint64(roundSeconds) * 1000;
    m_roundActive = true;
    m_timerPausedForOverlay = false;
    m_mpLocalSubmitted = false;
    m_mpRoundResolved = false;
    m_mpResultPanel->hide();
    if (m_mpActive) {
        m_mpHud->show();
        m_mpHud->raise();
        updateMpHud();
    }

    m_submitButton->setText(QStringLiteral("提交答案"));
    m_submitButton->setEnabled(true);
    refreshHud();

    const Question &question = m_roundQuestions.at(index);
    qInfo().noquote() << "回合开始" << (index + 1) << "/" << m_roundQuestions.size()
                      << "· 题目" << question.id
                      << "· 多人" << (m_mpActive ? "是" : "否")
                      << "· 限时" << (m_mpActive ? m_mpSeconds : m_secondsPerRound) << "秒";
    Sfx::play(QStringLiteral("round_start"), 0.7);
    m_screenshot->setSource(loadScreenshot(question, index + 1));
    m_viewStack->setCurrentWidget(m_screenshot);
    m_viewStack->show();
    m_hud->raise();
    m_noticeLabel->raise();

    if (m_mpActive || m_timerEnabled)
        m_timer->start();
    else
        m_timer->stop();

    setFocus(Qt::OtherFocusReason);
}

void GameWindow::refreshHud()
{
    updateAlarmOverlay();
    const int total = m_roundQuestions.size();
    const QString modeText = QStringLiteral("2D");
    if (m_mpActive) {
        m_roundLabel->setText(QStringLiteral("多人 · %1 · 第 %2/%3 回合 · 倍率 ×%4")
                                  .arg(modeText)
                                  .arg(m_roundIndex + 1)
                                  .arg(total)
                                  .arg(m_mpMultiplier, 0, 'f', 1));
    } else {
        m_roundLabel->setText(QStringLiteral("%1 模式 · 第 %2/%3 回合")
                                  .arg(modeText)
                                  .arg(m_roundIndex + 1)
                                  .arg(total));
    }

    if (m_mpActive || m_timerEnabled) {
        const QString color = m_remainingSeconds <= 10 ? QStringLiteral("#ff7b72")
                                                      : QStringLiteral("#e8f7ff");
        if (m_topTimer) {
            m_topTimer->setText(QStringLiteral("剩余 %1").arg(formatClock(m_remainingSeconds)));
            m_topTimer->setStyleSheet(QStringLiteral(
                "QLabel#topTimerChip {"
                "  background: rgba(5, 10, 15, 150);"
                "  color: %1;"
                "  border: 1px solid rgba(105, 224, 255, 75);"
                "  border-radius: 15px;"
                "  padding: 6px 18px;"
                "  font-family: Consolas, 'Cascadia Mono', monospace;"
                "  font-size: 18px; font-weight: 600;"
                "}").arg(color));
            m_topTimer->adjustSize();
            if (m_roundActive) {
                m_topTimer->show();
                m_topTimer->raise();
            } else {
                m_topTimer->hide();
            }
        }
    } else {
        if (m_topTimer)
            m_topTimer->hide();
    }
}

void GameWindow::updateNotice()
{
    QStringList lines;
    if (m_questionsMissing) {
        lines << QStringLiteral("未找到题库包：assets/data/questions.nteq · 已启用运行时占位测试图");
    } else if (!m_bankError.isEmpty()) {
        lines << QStringLiteral("%1 · 已启用运行时占位测试图").arg(m_bankError);
    }
    if (!AssetPaths::mapTilesAvailable())
        lines << QStringLiteral("未找到地图瓦片：assets/map/tiles · 地图将显示占位色");

    if (lines.isEmpty()) {
        m_noticeLabel->hide();
        return;
    }

    m_noticeLabel->setText(lines.join(QStringLiteral("\n")));
    m_noticeLabel->show();
    m_noticeLabel->raise();
    positionNotice();
}

void GameWindow::positionNotice()
{
    if (!m_noticeLabel || m_noticeLabel->isHidden())
        return;
    // 顶部提示居中，两侧留出空间。
    const int width = qMin(820, qMax(300, this->width() - 360));
    m_noticeLabel->setFixedWidth(width);
    m_noticeLabel->adjustSize();
    const int left = qMax(340, (this->width() - m_noticeLabel->width()) / 2);
    m_noticeLabel->move(left, 22);
    m_noticeLabel->raise();
}

void GameWindow::submitForTesting()
{
    if (!m_roundActive)
        return;
    if (!m_hasGuess) {
        m_guess = QPointF(0.44, 0.52);
        m_hasGuess = true;
    }
    submitAnswer();
}

void GameWindow::showFakeMpRoundForTesting()
{
    if (!m_overlay || !m_mpHud)
        return;

    // 造 4 名玩家，验证 3-4 人的顶部血条与结算（本地玩家本回合未作答）。
    m_mpState.clear();
    m_mpLocalId = 1;
    m_mpMultiplier = 3.0;
    const QVector<QPair<QString, QPair<int, int>>> roster{
        {QStringLiteral("测试"), {3200, 2553}},
        {QStringLiteral("玩家B"), {2900, 300}},
        {QStringLiteral("玩家C"), {2700, 500}},
        {QStringLiteral("慕君年"), {4100, 0}},
    };
    int nextId = 1;
    for (const auto &entry : roster) {
        MpPlayerState state;
        state.id = nextId++;
        state.name = entry.first;
        state.hp = GameWindow::MP_START_HP - entry.second.second;
        state.score = entry.second.first;
        state.damage = entry.second.second;
        state.submitted = entry.first != QStringLiteral("测试");
        state.hasMarker = state.submitted;
        m_mpState.append(state);
    }

    updateMpHud();
    m_mpHud->show();
    m_mpHud->raise();

    QVector<qreal> targets;
    const QVector<qreal> centers = m_mpHud->cellCentersX();
    for (qreal center : centers)
        targets.append(m_mpHud->x() + center);
    QVector<QRect> rects;
    MapOverlay::ReviewStyle style;
    const QVector<QRectF> bars = m_mpHud->barRects();
    for (int i = 0; i < bars.size(); ++i) {
        rects.append(QRect(qRound(m_mpHud->x() + bars.at(i).left()),
                           qRound(m_mpHud->y() + bars.at(i).bottom()) + 22,
                           qRound(bars.at(i).width()), 66));
        style.seatColors.append(BattleHudWidget::seatColor(i, i == 0));
    }
    style.damageTargetX = targets;
    style.damageTargetY = m_mpHud->y() + 32.0;
    style.cardRects = rects;
    style.localSeatColor = BattleHudWidget::seatColor(0, true);
    style.otherPinColors = {BattleHudWidget::seatColor(1, false),
                            BattleHudWidget::seatColor(2, false),
                            BattleHudWidget::seatColor(3, false)};

    const QPointF truth(0.450, 0.240);   // 故意放在地图上部，验证不会被分数框挡住
    const QPointF b(0.560, 0.360);
    const QPointF c(0.620, 0.640);
    const QPointF d(0.280, 0.700);
    // 本地玩家没标点：localHasGuess = false
    m_overlay->openForMultiplayerReview(mapButtonRectInWindow(), QPointF(0.5, 0.5), {b, c, d},
                                        truth, 0, QString(),
                                        {QStringLiteral("880 m"), QStringLiteral("1024 m"),
                                         QStringLiteral("1466 m")},
                                        {QStringLiteral("测试"), QStringLiteral("玩家B"),
                                         QStringLiteral("玩家C"), QStringLiteral("慕君年")},
                                        {3200, 2900, 2700, 4100}, {2553, 300, 500, 0}, 0,
                                        false, false, style);
    m_mpHud->raise();
}

void GameWindow::showHudForTesting()
{
    if (!m_mpHud)
        return;
    m_mpState.clear();
    m_mpLocalId = 1;
    m_mpMultiplier = 3.0;
    const QVector<QPair<QString, int>> roster{
        {QStringLiteral("测试"), 2680},
        {QStringLiteral("玩家B"), 4200},
        {QStringLiteral("玩家C"), 947},
        {QStringLiteral("慕君年"), 0},
    };
    int nextId = 1;
    for (const auto &entry : roster) {
        MpPlayerState state;
        state.id = nextId++;
        state.name = entry.first;
        state.hp = entry.second;
        state.active = entry.second > 0;
        m_mpState.append(state);
    }
    updateMpHud();
    m_mpHud->show();
    m_mpHud->raise();

    // 顺便把「最后 15 秒红色警报」也打开，方便截图自检。
    m_mpActive = true;
    m_roundActive = true;
    m_mpRoundResolved = false;
    m_remainingSeconds = 12;
    updateAlarmOverlay();
}

void GameWindow::showFakePauseForTesting()
{
    m_mpIsHost = true;
    m_mpActive = true;
    m_roundActive = true;
    m_mpPaused = true;
    m_pausedRemainingMs = 38000;
    beginPauseCountdown(QStringLiteral("测试2 掉线，等待重连…"), 30);
}

void GameWindow::showFakeLobbyForTesting()
{
    if (!m_mpSession)
        return;
    m_mpIsHost = true;
    QString error;
    if (!m_mpSession->startHost(QStringLiteral("测试1"), assetHash(), bankLabel(), &error, 45455)) {
        qWarning().noquote() << "自检开房失败:" << error;
        return;
    }
    if (m_startPage)
        m_startPage->show();
    showMpLobbyPage();
    updateMpLobby();
}

void GameWindow::showReconnectPromptForTesting()
{
    m_lastRoomAddress = QStringLiteral("192.168.1.20:45455");
    m_lastRoomName = QStringLiteral("测试");
    m_lastHostName = QStringLiteral("慕君年");
    m_mpIsHost = false;
    showStartMenu();
    showReconnectPrompt(QStringLiteral("房主已离开，房间已解散"));
}

void GameWindow::showFakeMpMatchEndForTesting()
{
    if (!m_mpHud)
        return;
    m_mpHud->show();
    m_mpHud->raise();
    QJsonArray players;
    const QVector<QPair<QString, int>> standings{
        {QStringLiteral("测试"), 2414},
        {QStringLiteral("慕君年"), 947},
        {QStringLiteral("玩家C"), 0},
    };
    int id = 1;
    for (const auto &entry : standings) {
        players.append(QJsonObject{
            {QStringLiteral("id"), id++},
            {QStringLiteral("name"), entry.first},
            {QStringLiteral("hp"), entry.second},
            {QStringLiteral("distance"), 0.0},
            {QStringLiteral("score"), 0},
            {QStringLiteral("damage"), 0},
            {QStringLiteral("submitted"), false},
            {QStringLiteral("hasMarker"), false},
            {QStringLiteral("alive"), entry.second > 0},
        });
    }
    const QJsonObject result{
        {QStringLiteral("type"), QStringLiteral("match_end")},
        {QStringLiteral("round"), 6},
        {QStringLiteral("multiplier"), 4.5},
        {QStringLiteral("matchEnd"), true},
        {QStringLiteral("finalRound"), true},
        {QStringLiteral("players"), players},
    };
    showMpRoundResult(result);
}

void GameWindow::openMapForPick()
{
    if (!m_roundActive || m_summaryPage->isVisible() || m_overlay->isOpen())
        return;

    // 多人模式提交后：只能打开地图查看，不能再改答案。
    if (m_mpActive && m_mpLocalSubmitted) {
        m_overlay->openForLockedView(mapButtonRectInWindow(), m_guess, m_hasGuess);
        return;
    }

    // 打开地图时不再暂停计时；顶部胶囊继续显示并保持在覆盖层之上。
    m_overlay->openForPick(mapButtonRectInWindow(), m_guess, m_hasGuess);
    if (m_topTimer) {
        m_topTimer->raise();
        m_topTimer->show();
    }
}

void GameWindow::resumeTimerAfterOverlay()
{
    if (m_timerEnabled && m_roundActive && m_timerPausedForOverlay) {
        m_timerPausedForOverlay = false;
        m_timer->start();
    }
}

void GameWindow::submitAnswer()
{
    if (!m_roundActive)
        return;
    if (!m_hasGuess) {
        openMapForPick();
        return;
    }
    if (m_mpActive) {
        if (m_mpLocalSubmitted)
            return;
        m_mpLocalSubmitted = true;
        m_submitButton->setEnabled(false);
        Sfx::play(QStringLiteral("submit"));
        QJsonObject message{
            {QStringLiteral("type"), QStringLiteral("guess")},
            {QStringLiteral("round"), m_mpRoundIndex},
            {QStringLiteral("point"), QJsonObject{
                {QStringLiteral("x"), m_guess.x()},
                {QStringLiteral("y"), m_guess.y()},
            }},
        };
        if (m_mpIsHost)
            handleMpGuess(m_mpLocalId, m_guess);
        else
            m_mpSession->send(message);
        return;
    }

    m_roundActive = false;
    m_timer->stop();
    m_timerPausedForOverlay = false;
    if (m_topTimer)
        m_topTimer->hide();
    m_submitButton->setEnabled(false);

    const Question &question = m_roundQuestions.at(m_roundIndex);
    const double distance = Score::distancePixels(m_guess, question.normalized);
    const double distanceM = Score::distanceMeters(m_guess, question.normalized);
    const int score = Score::scoreForDistanceMeters(distanceM, questionBankMaxDistanceMeters());
    Sfx::play(QStringLiteral("submit"));
    if (score >= 4000)
        Sfx::play(QStringLiteral("score_hit"), 0.8);

    RoundResult result;
    result.round = m_roundIndex + 1;
    result.guess = m_guess;
    result.truth = question.normalized;
    result.distancePx = distance;
    result.score = score;
    result.label = questionLabel(question);
    m_results.append(result);

    const QString resultHtml = QStringLiteral(
        "<span style='color:#93a8b2'>真实点</span> "
        "<span style='color:#ffcf7a'>(%1, %2)</span>"
        "　·　<span style='color:#93a8b2'>距离</span> "
        "<b style='color:#ffb454'>%3</b>"
        "　·　<span style='color:#93a8b2'>本题得分</span> "
        "<b style='color:#69e0ff'>%4</b> / 5000")
        .arg(question.normalized.x(), 0, 'f', 3)
        .arg(question.normalized.y(), 0, 'f', 3)
        .arg(Score::formatDistance(distance))
        .arg(score);

    const bool lastRound = (m_roundIndex + 1 >= m_roundQuestions.size());
    m_overlay->openForReview(mapButtonRectInWindow(), m_guess, question.normalized,
                             resultHtml, lastRound);
    qInfo().noquote() << "提交答案 · 回合" << (m_roundIndex + 1)
                      << "· 距离" << Score::formatDistance(distance)
                      << "· 得分" << score;
    // 单人模式不自动推进：由玩家点「下一题 / 查看结算」。
}

void GameWindow::showSummary()
{
    ++m_advanceToken;
    Sfx::play(QStringLiteral("match_end"), 0.9);
    m_roundActive = false;
    m_timer->stop();
    m_timerPausedForOverlay = false;
    if (m_viewStack)
        m_viewStack->hide();
    m_hud->hide();
    if (m_topTimer)
        m_topTimer->hide();
    m_noticeLabel->hide();

    clearSummaryRows();

    int total = 0;
    for (const RoundResult &result : m_results)
        total += result.score;

    const int maxScore = qMax(1, m_results.size()) * Score::MAX_SCORE_PER_ROUND;
    const QString rating = Score::ratingForTotal(total, m_results.size());
    m_summaryTotalLabel->setText(QStringLiteral("总分 %1 / %2").arg(total).arg(maxScore));

    QString ratingColor = QStringLiteral("#9fb3bd");
    if (rating == QStringLiteral("S"))
        ratingColor = QStringLiteral("#ffd166");
    else if (rating == QStringLiteral("A"))
        ratingColor = QStringLiteral("#69e0ff");
    else if (rating == QStringLiteral("B"))
        ratingColor = QStringLiteral("#9ee493");
    m_summaryRatingLabel->setStyleSheet(QStringLiteral(
        "font-size: 80px; font-weight: 800; color: %1;").arg(ratingColor));
    m_summaryRatingLabel->setText(rating);

    for (int i = 0; i < m_results.size(); ++i) {
        const RoundResult &result = m_results.at(i);
        auto *row = new QWidget(m_summaryPage);
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setStyleSheet(i % 2 == 0
            ? QStringLiteral("background: rgba(255,255,255,9); border-radius: 8px;")
            : QStringLiteral("background: rgba(255,255,255,5); border-radius: 8px;"));

        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(18, 12, 18, 12);
        rowLayout->setSpacing(16);

        auto *left = new QLabel(QStringLiteral("第 %1 题　%2")
                                    .arg(result.round)
                                    .arg(result.label), row);
        left->setStyleSheet(QStringLiteral("font-size: 14px; color: #dceaf0;"));
        rowLayout->addWidget(left, 1);

        auto *distanceLabel = new QLabel(Score::formatDistance(result.distancePx), row);
        distanceLabel->setAlignment(Qt::AlignCenter);
        distanceLabel->setMinimumWidth(130);
        distanceLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #93a8b2;"));
        rowLayout->addWidget(distanceLabel);

        auto *scoreLabel = new QLabel(QStringLiteral("%1 分").arg(result.score), row);
        scoreLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        scoreLabel->setMinimumWidth(100);
        scoreLabel->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #69e0ff;"));
        rowLayout->addWidget(scoreLabel);

        m_summaryRowsLayout->addWidget(row);
    }
    m_summaryRowsLayout->addStretch(1);

    m_summaryPage->setGeometry(rect());
    m_summaryPage->show();
    m_summaryPage->raise();
}

void GameWindow::clearSummaryRows()
{
    if (!m_summaryRowsLayout)
        return;
    while (QLayoutItem *item = m_summaryRowsLayout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            delete widget;
        delete item;
    }
}

QPixmap GameWindow::loadScreenshot(const Question &question, int roundNumber) const
{
    if (question.isPlaceholder)
        return makePlaceholderScreenshot(question.placeholderIndex, roundNumber,
                                         QStringLiteral("未找到题库 · 运行时占位测试图"));

    // 题库包里的图：按 media_id 从 media.ntep 取字节
    if (m_bank && !question.mediaId.isEmpty()) {
        const QByteArray bytes = m_bank->mediaBytes(question.mediaId);
        if (!bytes.isEmpty()) {
            QPixmap pixmap;
            if (pixmap.loadFromData(bytes))
                return pixmap;
        }
        return makePlaceholderScreenshot(1000 + roundNumber, roundNumber,
                                         QStringLiteral("题库包缺图：%1").arg(question.mediaId));
    }

    const QString path = AssetPaths::questionImage(question.imageRelative);
    QPixmap pixmap(path);
    if (pixmap.isNull()) {
        return makePlaceholderScreenshot(1000 + roundNumber, roundNumber,
                                         QStringLiteral("截图缺失：%1").arg(question.imageRelative));
    }
    return pixmap;
}

QPixmap GameWindow::makePlaceholderScreenshot(int seed, int roundNumber,
                                              const QString &caption) const
{
    const int width = 1600;
    const int height = 900;
    QImage image(width, height, QImage::Format_RGB32);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QLinearGradient background(0, 0, width, height);
    background.setColorAt(0.0, QColor(QStringLiteral("#061019")));
    background.setColorAt(0.5, QColor(QStringLiteral("#0d2630")));
    background.setColorAt(1.0, QColor(QStringLiteral("#173039")));
    painter.fillRect(image.rect(), background);

    QRandomGenerator random(quint32(seed * 7919 + 31));
    painter.setPen(QPen(QColor(105, 224, 255, 28), 2.0));
    for (int i = 0; i < 12; ++i) {
        const int y = random.bounded(height);
        painter.drawLine(QPointF(0, y), QPointF(width, y - 120));
    }
    for (int i = 0; i < 14; ++i) {
        const int w = 120 + random.bounded(380);
        const int h = 60 + random.bounded(260);
        const int x = random.bounded(width - w);
        const int y = 140 + random.bounded(420);
        QColor color(105, 224, 255, 10 + random.bounded(26));
        if (i % 3 == 0)
            color = QColor(255, 180, 84, 10 + random.bounded(22));
        painter.setPen(QPen(QColor(255, 255, 255, 18), 1.0));
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(x, y, w, h), 12, 12);
    }

    // 抽象地平线/建筑剪影，仅用于占位。
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(3, 8, 12, 190));
    painter.drawRect(QRect(0, 610, width, height - 610));
    for (int i = 0; i < 24; ++i) {
        const int w = 38 + random.bounded(84);
        const int h = 40 + random.bounded(210);
        const int x = i * 70 - 20;
        painter.drawRect(QRect(x, 620 - h, w, h + 20));
    }

    QRadialGradient vignette(QPointF(width / 2.0, height / 2.0), width * 0.68);
    vignette.setColorAt(0.0, QColor(0, 0, 0, 0));
    vignette.setColorAt(0.75, QColor(0, 0, 0, 30));
    vignette.setColorAt(1.0, QColor(0, 0, 0, 185));
    painter.fillRect(image.rect(), vignette);

    painter.setPen(QColor(235, 252, 255));
    QFont titleFont(QStringLiteral("Microsoft YaHei UI"), 46, QFont::DemiBold);
    painter.setFont(titleFont);
    painter.drawText(QRect(360, 80, 1120, 76),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("占位测试图 · 第 %1 张").arg(roundNumber));

    painter.setPen(QColor(205, 230, 237, 220));
    QFont captionFont(QStringLiteral("Microsoft YaHei UI"), 24);
    painter.setFont(captionFont);
    painter.drawText(QRect(362, 160, 1100, 100),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                     caption);

    painter.setPen(QColor(140, 170, 182, 180));
    QFont smallFont(QStringLiteral("Microsoft YaHei UI"), 19);
    painter.setFont(smallFont);
    painter.drawText(QRect(362, height - 130, 900, 30),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("运行时生成 · 不含任何游戏素材"));
    painter.end();

    return QPixmap::fromImage(image);
}

QString GameWindow::questionLabel(const Question &question) const
{
    if (!question.title.isEmpty())
        return question.title;
    if (!question.district.isEmpty() && !question.area.isEmpty())
        return QStringLiteral("%1 · %2").arg(question.district, question.area);
    if (!question.district.isEmpty())
        return question.district;
    if (!question.area.isEmpty())
        return question.area;
    if (!question.locationId.isEmpty())
        return question.locationId;
    return question.id;
}

QRect GameWindow::mapButtonRectInWindow() const
{
    if (m_mapButton)
        return QRect(m_mapButton->mapTo(this, QPoint(0, 0)), m_mapButton->size());
    return QRect(0, 0, 240, 240);
}

void GameWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    if (m_viewStack)
        m_viewStack->setGeometry(rect());

    if (m_alarm)
        m_alarm->setGeometry(rect());

    if (m_mapButton) {
        m_mapButton->adjustSize();
        m_mapButton->move(width() - m_mapButton->width() - 28,
                          height() - m_mapButton->height() - 118);
        m_mapButton->raise();
    }

    if (m_hud) {
        const int hudHeight = 72;
        const int hudWidth = qMin(1180, qMax(620, width() - 120));
        m_hud->setGeometry((width() - hudWidth) / 2,
                           height() - hudHeight - 26,
                           hudWidth, hudHeight);
        m_hud->raise();
    }

    if (m_topTimer) {
        m_topTimer->adjustSize();
        m_topTimer->move((width() - m_topTimer->width()) / 2, 14);
        m_topTimer->raise();
    }

    if (m_mpHud) {
        const int hudWidth = qMin(1180, qMax(680, width() - 80));
        m_mpHud->setFixedWidth(hudWidth);
        m_mpHud->move((width() - hudWidth) / 2, 64);
        m_mpHud->raise();
    }

    if (m_mpResultPanel) {
        m_mpResultPanel->setGeometry(rect());
        if (m_mpResultPanel->isVisible())
            m_mpResultPanel->raise();
    }

    if (m_countdownLabel && m_countdownLabel->isVisible()) {
        const int box = 260;
        m_countdownLabel->setGeometry((width() - box) / 2, (height() - box) / 2, box, box);
        m_countdownLabel->raise();
    }

    if (m_pauseLabel && m_pauseLabel->isVisible()) {
        m_pauseLabel->setGeometry(rect());
        m_pauseLabel->raise();
    }

    if (m_pauseSkipButton && m_pauseSkipButton->isVisible()) {
        m_pauseSkipButton->adjustSize();
        m_pauseSkipButton->move((width() - m_pauseSkipButton->width()) / 2,
                                height() / 2 + 90);
        m_pauseSkipButton->raise();
    }

    if (m_reconnectCard && m_reconnectCard->isVisible()) {
        const int cardWidth = qMin(470, qMax(320, width() - 120));
        m_reconnectCard->setFixedWidth(cardWidth);
        m_reconnectCard->adjustSize();
        const int cardHeight = m_reconnectCard->sizeHint().height();
        m_reconnectCard->setGeometry((width() - cardWidth) / 2,
                                     qMax(20, (height() - cardHeight) / 2),
                                     cardWidth, cardHeight);
        m_reconnectCard->raise();
    }

    if (m_summaryPage) {
        m_summaryPage->setGeometry(rect());
        if (m_summaryPage->isVisible())
            m_summaryPage->raise();
    }

    if (m_startPage) {
        m_startPage->setGeometry(rect());
        if (m_startBackground)
            m_startBackground->setGeometry(m_startPage->rect());
        if (m_startPage->isVisible())
            m_startPage->raise();
    }

    positionNotice();
}

void GameWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(QStringLiteral("#030507")));
}

void GameWindow::keyPressEvent(QKeyEvent *event)
{
    // F11 / Alt+Enter：全屏与窗口之间切换。
    const bool toggleFullScreen =
        event->key() == Qt::Key_F11
        || (event->key() == Qt::Key_Enter && (event->modifiers() & Qt::AltModifier))
        || (event->key() == Qt::Key_Return && (event->modifiers() & Qt::AltModifier));
    if (toggleFullScreen) {
        QSettings &settings = *PortableSettings::appSettings();
        if (isFullScreen()) {
            showNormal();
            const QByteArray saved = settings.value(QStringLiteral("game/windowGeometry")).toByteArray();
            if (!saved.isEmpty() && restoreGeometry(saved)) {
                // 恢复成功
            } else if (m_windowedGeometry.isValid()) {
                setGeometry(m_windowedGeometry);
            } else {
                resize(1440, 900);
            }
            show();
            settings.setValue(QStringLiteral("game/fullscreen"), false);
            settings.setValue(QStringLiteral("game/windowGeometry"), saveGeometry());
        } else {
            m_windowedGeometry = geometry();
            settings.setValue(QStringLiteral("game/windowGeometry"), saveGeometry());
            settings.setValue(QStringLiteral("game/fullscreen"), true);
            showFullScreen();
        }
        settings.sync();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        if (m_mpActive) {
            leaveMpRoom();
            showStartMenu();
            return;
        }
        if (m_startPage && m_startPage->isVisible()) {
            if (m_startStack && m_mainMenuPage
                && m_startStack->currentWidget() != m_mainMenuPage) {
                const QWidget *current = m_startStack->currentWidget();
                if (current == m_mpCreatePage || current == m_mpJoinPage
                    || current == m_mpLobbyPage) {
                    leaveMpRoom();
                }
                showStartMenu();
                return;
            }
            QCoreApplication::quit();
            return;
        }
        showStartMenu();
        return;
    }
    QWidget::keyPressEvent(event);
}