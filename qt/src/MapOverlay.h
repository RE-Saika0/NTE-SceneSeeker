#pragma once

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QWidget>

#include <functional>

class MapView;
class QLabel;
class QPushButton;
class QPropertyAnimation;
class QVariantAnimation;
class QHBoxLayout;

// 全屏地图覆盖层：从右下角地图按钮位置动画展开，选择模式下落 pin，回顾模式下显示真实点与连线。
class MapOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit MapOverlay(QWidget *parent = nullptr);

    // 多人结算的视觉参数：分数卡位置、每个座位的主色、扣血数字目标点。
    struct ReviewStyle {
        QVector<qreal> damageTargetX;    // 与分数卡对齐：血条格中心（窗口坐标）
        qreal damageTargetY = 74.0;
        QVector<QRect> cardRects;        // 与分数卡对齐：卡片矩形（窗口坐标）
        QVector<QColor> seatColors;      // 与分数卡对齐：该玩家的主题色
        QColor localSeatColor{46, 120, 221};
        QVector<QColor> otherPinColors;  // 与其他玩家标点对齐：标点颜色
    };

    void openForPick(const QRect &startRect, const QPointF &initialPin, bool hasPin);
    // 已提交答案后的只读查看：能缩放/拖动看地图，但不能改标点。
    void openForLockedView(const QRect &startRect, const QPointF &pin, bool hasPin);
    void openForReview(const QRect &startRect, const QPointF &guess, const QPointF &truth,
                       const QString &resultText, bool lastRound);
    void openForMultiplayerReview(const QRect &startRect, const QPointF &localGuess,
                                  const QVector<QPointF> &otherGuesses, const QPointF &truth,
                                  int localScore, const QString &localDistanceLabel,
                                  const QVector<QString> &otherDistanceLabels,
                                  const QVector<QString> &scoreNames, const QVector<int> &scoreValues,
                                  const QVector<int> &damageValues, int localIndex,
                                  bool lastRound, bool localHasGuess,
                                  const ReviewStyle &style);
    void closeImmediately();
    bool isOpen() const { return m_open; }
    // 是否是多人回合结算回放（决定血条在地图上面还是下面）。
    bool isMultiplayerReview() const { return m_multiplayerReview; }
    void resetMapViewForNewQuestion();

signals:
    void locationSelected(QPointF normalized);
    void pinSelected(QPointF normalized);
    void nextRequested();
    void cancelled();
    // 分数结算完毕、开始扣血的时刻（用于同步 HP 条动画）。
    void damagePhaseStarted();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void startOpen(const QRect &startRect);
    void finishOpen();
    // 把所有 reveal 动画量恢复到"完全显示"状态，避免上一次结算把标记归零后带进下一次。
    void resetReveals();
    void beginClose(std::function<void()> after);
    void updateLayout();
    void confirm();
    void cancel();
    void animateViewTo(const QPointF &center, double scale, int duration);
    void startScoreSequence();
    void spawnDamageFlyers();
    // 有 m_style.cardRects 时按"每张卡在本人血条下方"摆放，否则退回居中一排。
    void layoutScoreCards();
    // 清掉上一回合的分数卡 / 浮动数字，避免残留压在新内容下面。
    void clearScoreCards();
    void clearFloatingLabels();
    // 分数框在地图控件坐标系里占掉的顶部高度（含间距）。
    qreal scoreStripInset() const;

    MapView *m_mapView = nullptr;
    QWidget *m_bottomBar = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_scoreLabel = nullptr;
    QWidget *m_scoreStrip = nullptr;
    QHBoxLayout *m_scoreStripLayout = nullptr;
    QVector<QLabel *> m_scoreCards;
    QPushButton *m_confirmButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPropertyAnimation *m_animation = nullptr;
    QPropertyAnimation *m_otherPinsAnimation = nullptr;
    QPropertyAnimation *m_truthAnimation = nullptr;
    QPropertyAnimation *m_connectionAnimation = nullptr;
    QPropertyAnimation *m_distanceLabelsAnimation = nullptr;
    QVariantAnimation *m_scoreAnimation = nullptr;
    QVariantAnimation *m_zoomAnimation = nullptr;

    QRect m_startRect;
    QRect m_targetRect;
    QPointF m_initialPin{0.5, 0.5};
    bool m_hasInitialPin = false;
    QPointF m_guess{0.5, 0.5};
    QPointF m_truth{0.5, 0.5};

    bool m_review = false;
    bool m_multiplayerReview = false;
    bool m_lastRound = false;
    QVector<QPointF> m_resultOtherGuesses;
    int m_resultLocalScore = 0;
    QString m_resultLocalDistanceLabel;
    QVector<QString> m_resultOtherDistanceLabels;
    qreal m_lastTickProgress = 0.0;
    QVector<int> m_resultBestIndices;
    // 结算时飞向血条的扣血数字（用于中断后清理）
    QVector<QLabel *> m_flyLabels;
    bool m_reserveScoreArea = false;
    // 每张分数卡对应的血条格中心（窗口坐标）：扣血数字按这个位置飞。
    ReviewStyle m_style;
    // 本回合本地玩家是否真的标了点：没标就不画 pin（否则会画出上一回合/默认位置的点）。
    bool m_localHasGuess = true;
    // 只读查看（已提交答案）
    bool m_lockedView = false;
    QVector<QString> m_resultScoreNames;
    QVector<int> m_resultScoreValues;
    QVector<int> m_resultDamageValues;
    int m_resultLocalIndex = 0;
    bool m_open = false;
    bool m_animating = false;
    bool m_closing = false;
    bool m_hasSavedView = false;
    QPointF m_savedCenter;
    double m_savedScale = 0.0;
    bool m_savedFitMode = true;
    std::function<void()> m_afterClose;
};
