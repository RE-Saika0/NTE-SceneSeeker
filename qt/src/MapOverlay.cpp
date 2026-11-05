#include "MapOverlay.h"

#include "AssetPaths.h"
#include "MapView.h"
#include "SoundPlayer.h"

#include <QHBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QLabel>
#include <algorithm>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace {
// 读取 assets/data/map_labels.json 里已标好位置的路名。
QVector<MapView::Label> loadMapLabels()
{
    QVector<MapView::Label> labels;
    QFile file(AssetPaths::mapLabels());
    if (!file.open(QIODevice::ReadOnly)) {
        qInfo().noquote() << "未找到地图标签文件:" << AssetPaths::mapLabels();
        return labels;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &value : root.value(QStringLiteral("labels")).toArray()) {
        const QJsonObject item = value.toObject();
        if (!item.value(QStringLiteral("enabled")).toBool(true))
            continue;
        const QJsonValue x = item.value(QStringLiteral("x"));
        const QJsonValue y = item.value(QStringLiteral("y"));
        if (!x.isDouble() || !y.isDouble())
            continue;
        MapView::Label label;
        label.text = item.value(QStringLiteral("text")).toString();
        label.pos = QPointF(x.toDouble(), y.toDouble());
        label.angle = item.value(QStringLiteral("angle")).toDouble();
        label.size = item.value(QStringLiteral("size")).toInt(24);
        if (!label.text.isEmpty())
            labels.append(label);
    }
    qInfo().noquote() << "地图标签已加载:" << labels.size() << "条";
    return labels;
}

// 扣血数字从分数卡飞向血条的时长（毫秒）。血条动画等这批数字落地后再开始。
constexpr int kDamageFlyMs = 780;
} // namespace

MapOverlay::MapOverlay(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mapOverlay"));
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    hide();

    m_mapView = new MapView(this);
    m_mapView->setTileDirectory(AssetPaths::mapTilesDir());
    m_mapView->setLabels(loadMapLabels());

    m_bottomBar = new QWidget(this);
    m_bottomBar->setObjectName(QStringLiteral("overlayBar"));
    m_bottomBar->setAttribute(Qt::WA_StyledBackground, true);
    m_bottomBar->setStyleSheet(QStringLiteral(
        "QWidget#overlayBar {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(11,14,20,235), stop:1 rgba(15,23,42,245));"
        "  border: 1px solid rgba(56,189,248,65);"
        "  border-radius: 10px;"
        "}"
        "QPushButton {"
        "  background: rgba(56, 189, 248, 28);"
        "  color: #F8FAFC;"
        "  border: 1px solid rgba(56, 189, 248, 165);"
        "  border-radius: 7px;"
        "  padding: 9px 22px;"
        "  font-weight: 700; font-size: 14px;"
        "}"
        "QPushButton:hover { background: rgba(56, 189, 248, 58); }"
        "QPushButton:pressed { background: rgba(56, 189, 248, 88); }"
        "QPushButton:disabled { color: #687a84; border-color: rgba(110, 140, 155, 70); }"
        "QPushButton#secondaryButton {"
        "  background: rgba(30, 41, 59, 85);"
        "  color: #cbd5e1;"
        "  border-color: rgba(100, 116, 139, 120);"
        "}"
        "QPushButton#secondaryButton:hover { background: rgba(51, 65, 85, 105); }"
        "QLabel { color: #F8FAFC; background: transparent; font-weight: 500; }"
    ));

    auto *barLayout = new QHBoxLayout(m_bottomBar);
    barLayout->setContentsMargins(18, 10, 14, 10);
    barLayout->setSpacing(12);

    m_hintLabel = new QLabel(m_bottomBar);
    m_hintLabel->setTextFormat(Qt::RichText);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #cbd5e1; font-weight: 500;"));
    barLayout->addWidget(m_hintLabel, 1);

    m_scoreLabel = new QLabel(this);
    m_scoreLabel->setAlignment(Qt::AlignCenter);
    m_scoreLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_scoreLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: rgba(8,15,27,220); color: #F8FAFC;"
        " border: 2px solid rgba(103,232,249,200); border-radius: 14px;"
        " padding: 10px 28px; font-size: 30px; font-weight: 900; }"));
    m_scoreLabel->hide();

    m_scoreStrip = new QWidget(this);
    m_scoreStrip->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_scoreStripLayout = new QHBoxLayout(m_scoreStrip);
    m_scoreStripLayout->setContentsMargins(0, 0, 0, 0);
    m_scoreStripLayout->setSpacing(12);
    m_scoreStrip->hide();

    m_cancelButton = new QPushButton(QStringLiteral("取消"), m_bottomBar);
    m_cancelButton->setObjectName(QStringLiteral("secondaryButton"));
    m_cancelButton->setMinimumSize(88, 40);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    barLayout->addWidget(m_cancelButton);

    m_confirmButton = new QPushButton(QStringLiteral("确认"), m_bottomBar);
    m_confirmButton->setMinimumSize(116, 40);
    m_confirmButton->setCursor(Qt::PointingHandCursor);
    m_confirmButton->setDefault(true);
    barLayout->addWidget(m_confirmButton);

    connect(m_cancelButton, &QPushButton::clicked, this, &MapOverlay::cancel);
    connect(m_confirmButton, &QPushButton::clicked, this, &MapOverlay::confirm);

    m_animation = new QPropertyAnimation(this, "geometry", this);
    m_animation->setDuration(200);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
        if (m_closing) {
            m_animating = false;
            m_open = false;
            hide();
            m_mapView->setResizeSuspended(false);
            m_confirmButton->setEnabled(true);
            m_cancelButton->setEnabled(true);
            auto callback = std::move(m_afterClose);
            m_afterClose = nullptr;
            if (callback)
                callback();
        } else {
            finishOpen();
        }
    });

    connect(m_mapView, &MapView::mapClicked, this, [this](const QPointF &normalized) {
        if (m_review || !m_open)
            return;
        m_mapView->setPin(normalized, true);
        m_guess = normalized;
        m_hasInitialPin = true;
        emit pinSelected(normalized);
        m_confirmButton->setEnabled(true);
        m_hintLabel->setText(QStringLiteral(
            "已记录位置 · 可拖动 pin 微调 · <span style='color:#8fa3ad'>Enter 关闭地图 / Esc 关闭</span>"));
    });
    connect(m_mapView, &MapView::pinChanged, this, [this](const QPointF &normalized) {
        if (!m_review) {
            m_guess = normalized;
            m_hasInitialPin = true;
            emit pinSelected(normalized);
            m_confirmButton->setEnabled(true);
            m_hintLabel->setText(QStringLiteral("已记录位置 · 可拖动 pin 微调"));
        }
    });

    auto *enterShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    enterShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(enterShortcut, &QShortcut::activated, this, &MapOverlay::confirm);

    auto *enterKeypad = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    enterKeypad->setContext(Qt::WidgetWithChildrenShortcut);
    connect(enterKeypad, &QShortcut::activated, this, &MapOverlay::confirm);

    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escShortcut, &QShortcut::activated, this, &MapOverlay::cancel);
}

void MapOverlay::openForPick(const QRect &startRect, const QPointF &initialPin, bool hasPin)
{
    m_review = false;
    m_multiplayerReview = false;
    m_reserveScoreArea = false;
    m_lockedView = false;
    m_lastRound = false;
    m_hasInitialPin = hasPin;
    m_initialPin = initialPin;

    m_mapView->setPinEditable(true);
    m_mapView->clearTruthPoint();
    m_mapView->setShowConnection(false);
    m_mapView->clearOtherPins();
    m_mapView->setPinColor(QColor(QStringLiteral("#69e0ff")));
    // 结算动画会把 reveal 归零；不重置的话下一次开图会"看不见标点"。
    resetReveals();
    if (hasPin)
        m_mapView->setPin(initialPin, true);
    else
        m_mapView->clearPin();

    m_confirmButton->setText(QStringLiteral("关闭地图"));
    m_confirmButton->setEnabled(true);
    m_confirmButton->show();
    m_cancelButton->show();
    m_cancelButton->setEnabled(true);
    m_hintLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #cbd5e1; font-weight: 500;"));
    m_hintLabel->setText(hasPin
        ? QStringLiteral("已记录位置 · 拖动 pin 微调 · 滚轮缩放 · <span style='color:#94a3b8'>Esc 关闭</span>")
        : QStringLiteral("点击地图落点即记录答案 · 拖动 pin 微调 · <span style='color:#94a3b8'>Esc 关闭</span>"));

    startOpen(startRect);
}

void MapOverlay::openForLockedView(const QRect &startRect, const QPointF &pin, bool hasPin)
{
    m_review = true;
    m_multiplayerReview = false;
    m_reserveScoreArea = false;
    m_lockedView = true;
    m_lastRound = false;
    m_hasInitialPin = hasPin;
    m_initialPin = pin;
    m_guess = pin;

    m_mapView->setPinEditable(false);
    m_mapView->clearTruthPoint();
    m_mapView->setShowConnection(false);
    m_mapView->clearOtherPins();
    m_mapView->setPinColor(QColor(QStringLiteral("#69e0ff")));
    resetReveals();
    if (hasPin)
        m_mapView->setPin(pin, true);
    else
        m_mapView->clearPin();

    m_confirmButton->setText(QStringLiteral("关闭地图"));
    m_confirmButton->setEnabled(true);
    m_confirmButton->show();
    m_cancelButton->hide();
    m_hintLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #CBD5E1; font-weight: 500;"));
    m_hintLabel->setText(QStringLiteral(
        "本回合答案已提交，不能再修改 · 可以拖动/缩放查看地图"));

    startOpen(startRect);
}

void MapOverlay::resetMapViewForNewQuestion()
{
    m_hasSavedView = false;
    m_mapView->resetView();
    // 新一轮开始前把上一轮的结算残留清干净。
    clearScoreCards();
    clearFloatingLabels();
    if (m_scoreStrip)
        m_scoreStrip->hide();
}

void MapOverlay::openForReview(const QRect &startRect, const QPointF &guess, const QPointF &truth,
                               const QString &resultText, bool lastRound)
{
    m_review = true;
    m_multiplayerReview = false;
    m_reserveScoreArea = false;
    m_lockedView = false;
    m_lastRound = lastRound;
    m_guess = guess;
    m_truth = truth;

    m_mapView->setPinEditable(false);
    m_mapView->setPin(guess, true);
    m_mapView->setTruthPoint(truth, true);
    m_mapView->setShowConnection(true);
    m_mapView->clearOtherPins();
    m_mapView->setPinColor(QColor(QStringLiteral("#69e0ff")));
    // 上次多人结算把 reveal 归零过，这里必须恢复，否则目标点不显示。
    resetReveals();

    m_confirmButton->setText(lastRound ? QStringLiteral("查看结算") : QStringLiteral("下一题"));
    m_confirmButton->setEnabled(true);
    m_confirmButton->show();
    m_cancelButton->hide();
    m_hintLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #F8FAFC; font-weight: 600;"));
    m_hintLabel->setText(resultText);

    startOpen(startRect);
}

void MapOverlay::openForMultiplayerReview(const QRect &startRect, const QPointF &localGuess,
                                          const QVector<QPointF> &otherGuesses,
                                          const QPointF &truth, int localScore,
                                          const QString &localDistanceLabel,
                                          const QVector<QString> &otherDistanceLabels,
                                          const QVector<QString> &scoreNames, const QVector<int> &scoreValues,
                                          const QVector<int> &damageValues, int localIndex,
                                          bool lastRound, bool localHasGuess,
                                          const ReviewStyle &style)
{
    m_review = true;
    m_multiplayerReview = true;
    m_reserveScoreArea = true;
    m_lockedView = false;
    m_lastRound = lastRound;
    m_localHasGuess = localHasGuess;
    m_style = style;
    m_guess = localGuess;
    m_truth = truth;
    m_resultOtherGuesses = otherGuesses;
    m_resultLocalScore = localScore;
    m_resultLocalDistanceLabel = localDistanceLabel;
    m_resultOtherDistanceLabels = otherDistanceLabels;
    m_resultScoreNames = scoreNames;
    m_resultScoreValues = scoreValues;
    m_resultDamageValues = damageValues;
    m_resultLocalIndex = localIndex;

    m_mapView->setPinEditable(false);
    m_mapView->setPin(localGuess, localHasGuess); // Step 1: local guess only（没标点就不画）
    m_mapView->clearOtherPins();
    // 每个玩家用自己的主题色（和顶部血条头像一致）。
    m_mapView->setPinColor(m_style.localSeatColor);
    m_mapView->setOtherPinColors(m_style.otherPinColors);
    m_mapView->clearTruthPoint();
    m_mapView->setShowConnection(false);
    m_mapView->setOtherPinsReveal(0.0);
    m_mapView->setTruthReveal(0.0);
    m_mapView->setConnectionReveal(0.0);
    m_mapView->setDistanceLabels(localDistanceLabel, otherDistanceLabels);
    m_mapView->setDistanceLabelsReveal(0.0);
    m_confirmButton->hide();
    m_cancelButton->hide();
    m_hintLabel->setText(QStringLiteral("回合结算中…"));
    m_scoreLabel->hide();
    clearScoreCards();
    clearFloatingLabels();
    const bool manualCards = (m_style.cardRects.size() == m_resultScoreValues.size());
    for (int i = 0; i < m_resultScoreNames.size() && i < m_resultScoreValues.size(); ++i) {
        auto *card = new QLabel(m_scoreStrip);
        card->setTextFormat(Qt::RichText);
        card->setAlignment(Qt::AlignCenter);
        card->setMinimumHeight(66);
        card->setMinimumWidth(150);
        card->setStyleSheet(QStringLiteral(
            "QLabel { background: rgba(8,15,27,224); color: #F8FAFC;"
            " border: 1px solid rgba(103,232,249,170); border-radius: 9px;"
            " padding: 4px 16px; }"));
        if (!manualCards)
            m_scoreStripLayout->addWidget(card, 1);
        m_scoreCards.append(card);
    }
    layoutScoreCards();
    m_scoreStrip->hide();
    startOpen(startRect);
}

// 有 cardRects 时：每张卡直接摆在本人血条正下方（跟顶部血条一格一格对齐）。
// 清掉上一回合的分数卡。注意：手动定位模式下卡片没进 layout，
// 只清 layout 会漏掉它们（表现就是新卡片下面压着一排旧的）。
void MapOverlay::clearScoreCards()
{
    while (QLayoutItem *item = m_scoreStripLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    const QList<QWidget *> leftovers =
        m_scoreStrip->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *widget : leftovers) {
        widget->hide();
        widget->deleteLater();
    }
    m_scoreCards.clear();
}

// 清掉还在飞的扣血数字（动画被打断时会留在覆盖层上）。
void MapOverlay::clearFloatingLabels()
{
    for (QLabel *label : m_flyLabels) {
        if (!label)
            continue;
        label->hide();
        label->deleteLater();
    }
    m_flyLabels.clear();
}

void MapOverlay::layoutScoreCards()
{
    if (m_scoreCards.isEmpty())
        return;
    const bool manual = (m_style.cardRects.size() == m_scoreCards.size());
    if (!manual) {
        // 退化成原来的居中一排
        const int stripWidth = qMin(760, qMax(320, width() - 200));
        m_scoreStrip->setGeometry((width() - stripWidth) / 2, 150, stripWidth, 76);
        return;
    }
    QRect area;
    for (const QRect &rect : m_style.cardRects)
        area = area.isNull() ? rect : area.united(rect);
    m_scoreStrip->setGeometry(area);
    for (int i = 0; i < m_scoreCards.size(); ++i) {
        const QRect rect = m_style.cardRects.at(i).translated(-area.topLeft());
        m_scoreCards.at(i)->setGeometry(rect);
        m_scoreCards.at(i)->show();
    }
}

qreal MapOverlay::scoreStripInset() const
{
    if (!m_reserveScoreArea || !m_scoreStrip || !m_mapView)
        return 0.0;
    const QRect strip = m_scoreStrip->geometry();
    if (strip.isEmpty())
        return 0.0;
    const int mapTop = m_mapView->geometry().top();
    const int covered = strip.bottom() + 12 - mapTop; // 12px 安全间距
    return qMax<qreal>(0.0, covered);
}

void MapOverlay::closeImmediately()
{
    if (m_animation->state() == QAbstractAnimation::Running)
        m_animation->stop();
    m_open = false;
    m_animating = false;
    m_closing = false;
    m_afterClose = nullptr;
    m_scoreLabel->hide();
    m_scoreStrip->hide();
    // Result-only annotations must not bleed into the following round.
    m_mapView->setDistanceLabels(QString(), {});
    m_mapView->setDistanceLabelsReveal(0.0);
    m_mapView->setOtherPinsReveal(0.0);
    m_mapView->setTruthReveal(0.0);
    m_mapView->setConnectionReveal(0.0);
    m_mapView->clearOtherPins();
    m_style = ReviewStyle();
    clearScoreCards();
    clearFloatingLabels();
    m_multiplayerReview = false;
    hide();
}

void MapOverlay::resetReveals()
{
    m_mapView->setOtherPinsReveal(1.0);
    m_mapView->setTruthReveal(1.0);
    m_mapView->setConnectionReveal(1.0);
    m_mapView->setDistanceLabelsReveal(1.0);
}

void MapOverlay::startOpen(const QRect &startRect)
{
    if (m_animation->state() == QAbstractAnimation::Running)
        m_animation->stop();

    m_startRect = startRect.isValid() ? startRect : QRect(0, 0, 240, 240);
    const QRect target = parentWidget() ? parentWidget()->rect() : QRect(0, 0, 1280, 720);
    m_targetRect = target;

    // 先把地图撑到「最终尺寸」再算视图（否则第一次打开会按小尺寸算 fit，看起来是缩小的）。
    m_animating = true;
    m_closing = false;
    m_mapView->setResizeSuspended(true);
    updateLayout();

    if (m_hasSavedView)
        m_mapView->setViewState(m_savedCenter, m_savedScale, m_savedFitMode);
    else if (m_hasInitialPin)
        m_mapView->focusOn(m_initialPin);
    else
        // 首次打开：默认落在城区中心并放大到能看清街道，而不是缩成整张全图。
        m_mapView->focusOn(QPointF(0.44, 0.52), qMax(m_mapView->currentScale(), 0.15));

    setGeometry(m_startRect);
    show();
    raise();
    setFocus(Qt::OtherFocusReason);

    m_confirmButton->setEnabled(false);
    m_cancelButton->setEnabled(false);
    m_open = true;

    m_animation->setStartValue(m_startRect);
    m_animation->setEndValue(target);
    m_animation->start();
}

void MapOverlay::finishOpen()
{
    m_mapView->setResizeSuspended(false);
    m_animating = false;
    m_confirmButton->setEnabled(m_review || m_mapView->hasPin());
    m_cancelButton->setEnabled(true);

    if (m_review) {
        if (m_multiplayerReview) {
            // 结算回放：先回到"玩家关闭地图时的视角"，再聚焦自己的标点，
            // 然后拉到能装下所有标点的 80% 区域，最后揭示目标点与分数。
            QTimer::singleShot(120, this, [this]() {
                if (!m_open || !m_multiplayerReview)
                    return;
                // setViewState 使用世界像素坐标（0..26112），归一化点需要换算。
                // 没标点的话就聚焦目标点，而不是一个不存在的"自己的点"。
                const QPointF focus = m_localHasGuess ? m_guess : m_truth;
                const QPointF worldCenter(focus.x() * MapView::WORLD,
                                          focus.y() * MapView::WORLD);
                animateViewTo(worldCenter, qMax(m_mapView->currentScale(), 0.12), 620);
            });
            QTimer::singleShot(900, this, [this]() {
                if (!m_open || !m_multiplayerReview)
                    return;
                QVector<QPointF> all = m_resultOtherGuesses;
                if (m_localHasGuess)
                    all.append(m_guess);
                all.append(m_truth); // 目标点也要落在 80% 可视区内
                m_mapView->setOtherPins(m_resultOtherGuesses);
                auto *pins = new QVariantAnimation(this);
                pins->setDuration(380);
                pins->setStartValue(0.0);
                pins->setEndValue(1.0);
                connect(pins, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
                    m_mapView->setOtherPinsReveal(v.toReal());
                });
                pins->start(QAbstractAnimation::DeleteWhenStopped);
                QPointF center;
                double scale = 0.0;
                // 分数框盖住的那一条不让标点进去：地图尺寸/布局不变，只是把「可视区」下移到分数框下方。
                const qreal topInset = scoreStripInset();
                if (m_mapView->fitTargetForPoints(all, 0.10, &center, &scale, topInset))
                    animateViewTo(center, scale, 700);
            });
            QTimer::singleShot(1700, this, [this]() {
                if (!m_open || !m_multiplayerReview)
                    return;
                m_mapView->setTruthPoint(m_truth, true);
                m_mapView->setShowConnection(true);
                auto *connection = new QVariantAnimation(this);
                connection->setDuration(560);
                connection->setStartValue(0.0);
                connection->setEndValue(1.0);
                connect(connection, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
                    m_mapView->setConnectionReveal(v.toReal());
                });
                connection->start(QAbstractAnimation::DeleteWhenStopped);
                // 目标点先弹大再回落。
                auto *pop = new QVariantAnimation(this);
                pop->setDuration(520);
                pop->setStartValue(0.0);
                pop->setEndValue(1.35);
                pop->setEasingCurve(QEasingCurve::OutCubic);
                connect(pop, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
                    m_mapView->setTruthReveal(v.toReal());
                });
                auto *settle = new QVariantAnimation(this);
                settle->setDuration(260);
                settle->setStartValue(1.35);
                settle->setEndValue(1.0);
                settle->setEasingCurve(QEasingCurve::InOutCubic);
                connect(settle, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
                    m_mapView->setTruthReveal(v.toReal());
                });
                connect(pop, &QVariantAnimation::finished, this, [settle]() { settle->start(); });
                pop->start(QAbstractAnimation::DeleteWhenStopped);
                m_hintLabel->setText(QStringLiteral("目标位置"));
                // 距离数字跟着目标点弹出一起淡入。
                m_mapView->setDistanceLabels(m_resultLocalDistanceLabel, m_resultOtherDistanceLabels);
                auto *labels = new QVariantAnimation(this);
                labels->setDuration(420);
                labels->setStartValue(0.0);
                labels->setEndValue(1.0);
                connect(labels, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
                    m_mapView->setDistanceLabelsReveal(v.toReal());
                });
                labels->start(QAbstractAnimation::DeleteWhenStopped);
            });
            QTimer::singleShot(2450, this, [this]() {
                if (!m_open || !m_multiplayerReview)
                    return;
                startScoreSequence();
            });
        } else {
            // 只读查看：保持 startOpen 里定好的视角，不再 fit 到目标点。
            if (!m_lockedView)
                m_mapView->fitToPoints(m_guess, m_truth);
        }
    } else if (!m_hasSavedView) {
        // 视图已在 startOpen 里设置
    }
    // 选点模式的视图已经在 startOpen 里按目标尺寸设好了，这里不再覆盖。

    setFocus(Qt::OtherFocusReason);
}

void MapOverlay::beginClose(std::function<void()> after)
{
    if (!m_open || m_animating)
        return;

    m_savedCenter = m_mapView->viewCenter();
    m_savedScale = m_mapView->currentScale();
    m_savedFitMode = m_mapView->isFitMode();
    m_hasSavedView = true;

    m_closing = true;
    m_animating = true;
    m_afterClose = std::move(after);
    m_confirmButton->setEnabled(false);
    m_cancelButton->setEnabled(false);
    m_mapView->setResizeSuspended(true);

    m_animation->stop();
    m_animation->setStartValue(geometry());
    m_animation->setEndValue(m_startRect);
    m_animation->start();
}

void MapOverlay::confirm()
{
    if (m_animating || !m_open)
        return;

    if (m_review) {
        beginClose([this]() { emit nextRequested(); });
        return;
    }

    if (!m_mapView->hasPin()) {
        m_hintLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #ffb454;"));
        m_hintLabel->setText(QStringLiteral("请先在地图上选择一个位置。"));
        return;
    }

    const QPointF selected = m_mapView->pin();
    beginClose([this, selected]() { emit locationSelected(selected); });
}

void MapOverlay::cancel()
{
    if (m_animating || !m_open)
        return;

    if (m_review) {
        beginClose([this]() { emit nextRequested(); });
        return;
    }

    beginClose([this]() { emit cancelled(); });
}

void MapOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 180));
}

void MapOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLayout();
}

void MapOverlay::animateViewTo(const QPointF &center, double scale, int duration)
{
    const QPointF startCenter = m_mapView->viewCenter();
    const double startScale = m_mapView->currentScale();
    auto *anim = new QVariantAnimation(this);
    anim->setDuration(duration);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::InOutCubic);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this, startCenter, center, startScale, scale](const QVariant &value) {
        const qreal t = value.toReal();
        m_mapView->setViewState(startCenter + (center - startCenter) * t,
                                startScale + (scale - startScale) * t, false);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MapOverlay::startScoreSequence()
{
    if (m_scoreCards.isEmpty())
        return;

    m_lastTickProgress = 0.0;
    Sfx::play(QStringLiteral("score_tick"), 0.4);

    updateLayout();          // 分数框占用的区域已在布局里预留
    m_scoreStrip->show();
    m_scoreStrip->raise();

    auto *count = new QVariantAnimation(this);
    count->setDuration(900);
    count->setStartValue(0.0);
    count->setEndValue(1.0);
    count->setEasingCurve(QEasingCurve::OutCubic);
    connect(count, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        const qreal progress = value.toReal();
        if (progress - m_lastTickProgress > 0.07) {
            m_lastTickProgress = progress;
            Sfx::play(QStringLiteral("score_tick"), 0.5);
        }
        for (int i = 0; i < m_scoreCards.size(); ++i) {
            const int score = i < m_resultScoreValues.size()
                ? qRound(m_resultScoreValues.at(i) * progress) : 0;
            const QString name = i < m_resultScoreNames.size()
                ? m_resultScoreNames.at(i).toHtmlEscaped() : QStringLiteral("玩家");
            m_scoreCards.at(i)->setText(QStringLiteral(
                "<div style='color:#9FB3C8;font-size:12px;font-weight:600'>%1</div>"
                "<div style='color:#F8FAFC;font-size:30px;font-weight:800'>%2</div>")
                .arg(name)
                .arg(score));
        }
    });
    count->start(QAbstractAnimation::DeleteWhenStopped);

    // 数字跳到最终值后：先左右抖动（0.3s），再由最高分卡"分裂"出数字飞向其他卡（0.5s）。
    QTimer::singleShot(950, this, [this]() {
        if (!m_open || !m_multiplayerReview || m_scoreCards.isEmpty())
            return;

        QVector<QPoint> base;
        base.reserve(m_scoreCards.size());
        for (QLabel *card : m_scoreCards)
            base.append(card->pos());

        auto *shake = new QVariantAnimation(this);
        shake->setDuration(300);
        shake->setStartValue(0.0);
        shake->setEndValue(1.0);
        connect(shake, &QVariantAnimation::valueChanged, this, [this, base](const QVariant &value) {
            const qreal t = value.toReal();
            const qreal amplitude = 9.0 * (1.0 - t);
            for (int i = 0; i < m_scoreCards.size() && i < base.size(); ++i) {
                const qreal dx = std::sin(t * 40.0 + i * 1.7) * amplitude;
                m_scoreCards.at(i)->move(base.at(i) + QPoint(qRound(dx), 0));
            }
        });
        shake->start(QAbstractAnimation::DeleteWhenStopped);

        // 抖动结束 → 最高分数字飞向其余卡片
        QTimer::singleShot(320, this, [this, base]() {
            if (!m_open || !m_multiplayerReview)
                return;
            for (int i = 0; i < m_scoreCards.size() && i < base.size(); ++i)
                m_scoreCards.at(i)->move(base.at(i));

            const int maxScore = *std::max_element(m_resultScoreValues.cbegin(),
                                                   m_resultScoreValues.cend());
            QVector<int> bests;
            for (int i = 0; i < m_resultScoreValues.size(); ++i) {
                if (m_resultScoreValues.at(i) == maxScore)
                    bests.append(i);
            }
            const int sourceIndex = bests.isEmpty() ? 0 : bests.first();
            m_resultBestIndices = bests;

            // 从最高分卡片起点，向每个非最高分卡片飞出"差额数字"
            for (int i = 0; i < m_scoreCards.size(); ++i) {
                if (bests.contains(i))
                    continue;
                const int score = i < m_resultScoreValues.size() ? m_resultScoreValues.at(i) : 0;
                const qint64 diff = qMax<qint64>(0, maxScore - score);
                if (diff <= 0)
                    continue;

                auto *fly = new QLabel(this);
                m_flyLabels.append(fly);
                fly->setAttribute(Qt::WA_TransparentForMouseEvents);
                fly->setText(QStringLiteral("%1").arg(diff));
                fly->setStyleSheet(QStringLiteral(
                    "QLabel { color: #FACC15; font-size: 30px; font-weight: 900;"
                    " background: transparent; }"));
                fly->adjustSize();

                const QPoint from = m_scoreCards.at(sourceIndex)->mapTo(this, QPoint(0, 0))
                    + QPoint((m_scoreCards.at(sourceIndex)->width() - fly->width()) / 2, 0);
                const QPoint to = m_scoreCards.at(i)->mapTo(this, QPoint(0, 0))
                    + QPoint((m_scoreCards.at(i)->width() - fly->width()) / 2, 0);
                fly->move(from);
                fly->show();
                fly->raise();

                auto *effect = new QGraphicsOpacityEffect(fly);
                effect->setOpacity(1.0);
                fly->setGraphicsEffect(effect);

                auto *move = new QPropertyAnimation(fly, "pos", fly);
                move->setDuration(520);
                move->setStartValue(from);
                move->setEndValue(to);
                move->setEasingCurve(QEasingCurve::InOutCubic);
                auto *fade = new QPropertyAnimation(effect, "opacity", fly);
                fade->setDuration(520);
                fade->setStartValue(1.0);
                fade->setEndValue(0.15);
                fade->setEasingCurve(QEasingCurve::OutCubic);
                move->start(QAbstractAnimation::DeleteWhenStopped);
                fade->start(QAbstractAnimation::DeleteWhenStopped);
                connect(move, &QPropertyAnimation::finished, fly, [this, fly]() {
                    m_flyLabels.removeAll(fly);
                    fly->deleteLater();
                });
            }

            // 数字落位后：最高分（可能有并列）保持金色，其余变红并开始扣血
            QTimer::singleShot(520, this, [this, base]() {
                if (!m_open || !m_multiplayerReview)
                    return;

                for (int i = 0; i < m_scoreCards.size() && i < base.size(); ++i) {
                    m_scoreCards.at(i)->move(base.at(i));
                    const QString name = i < m_resultScoreNames.size()
                        ? m_resultScoreNames.at(i).toHtmlEscaped() : QStringLiteral("玩家");
                    const int score = i < m_resultScoreValues.size()
                        ? m_resultScoreValues.at(i) : 0;
                    if (m_resultBestIndices.contains(i)) {
                        m_scoreCards.at(i)->setStyleSheet(QStringLiteral(
                            "QLabel { background: rgba(8,15,27,235); color: #F8FAFC;"
                            " border: 2px solid rgba(250,204,21,210); border-radius: 9px;"
                            " padding: 4px 16px; }"));
                        m_scoreCards.at(i)->setText(QStringLiteral(
                            "<div style='color:#FACC15;font-size:12px;font-weight:700'>%1 · 最高分</div>"
                            "<div style='color:#FFFFFF;font-size:30px;font-weight:800'>%2</div>")
                            .arg(name)
                            .arg(score));
                    } else {
                        const int damage = i < m_resultDamageValues.size()
                            ? m_resultDamageValues.at(i) : 0;
                        m_scoreCards.at(i)->setStyleSheet(QStringLiteral(
                            "QLabel { background: rgba(40,10,16,210); color: #FCA5A5;"
                            " border: 1px solid rgba(248,113,113,170); border-radius: 9px;"
                            " padding: 4px 16px; }"));
                        m_scoreCards.at(i)->setText(QStringLiteral(
                            "<div style='color:#F87171;font-size:12px;font-weight:600'>%1</div>"
                            "<div style='color:#FCA5A5;font-size:30px;font-weight:800'>−%2</div>")
                            .arg(name)
                            .arg(damage));
                    }
                }

                spawnDamageFlyers();
                Sfx::play(QStringLiteral("score_hit"), 0.8);
            });
        });
    });
}

void MapOverlay::spawnDamageFlyers()
{
    if (m_scoreCards.isEmpty())
        return;

    // 并列最高分的都要豁免（都按最高分算，不扣血）。
    QVector<int> bests = m_resultBestIndices;
    if (bests.isEmpty()) {
        const int maxScore = *std::max_element(m_resultScoreValues.cbegin(),
                                               m_resultScoreValues.cend());
        for (int i = 0; i < m_resultScoreValues.size(); ++i) {
            if (m_resultScoreValues.at(i) == maxScore)
                bests.append(i);
        }
    }

    // 扣血数字先飞向血条；血条动画等数字落到血条上再开始（晚 780ms）。
    Sfx::play(QStringLiteral("damage"), 0.9);

    for (int i = 0; i < m_scoreCards.size(); ++i) {
        if (bests.contains(i))
            continue;
        const int damage = i < m_resultDamageValues.size() ? m_resultDamageValues.at(i) : 0;
        if (damage <= 0)
            continue;

        auto *fly = new QLabel(this);
        m_flyLabels.append(fly);
        fly->setAttribute(Qt::WA_TransparentForMouseEvents);
        fly->setText(QStringLiteral("−%1").arg(damage));
        fly->setStyleSheet(QStringLiteral(
            "QLabel { color: #FF6B7A; font-size: 34px; font-weight: 900;"
            " background: transparent; }"));
        fly->adjustSize();

        QPoint start = m_scoreCards.at(i)->mapTo(this, QPoint(0, 0));
        start += QPoint((m_scoreCards.at(i)->width() - fly->width()) / 2, 0);
        fly->move(start);
        fly->show();
        fly->raise();

        auto *effect = new QGraphicsOpacityEffect(fly);
        effect->setOpacity(1.0);
        fly->setGraphicsEffect(effect);

        // 飞向这名玩家自己的那一格血条（3-4 人局也能对准）；没有目标坐标时退回左/右半边。
        const qreal fallbackX = (i == m_resultLocalIndex) ? width() / 4.0 : width() * 3.0 / 4.0;
        const qreal targetCenterX = (i < m_style.damageTargetX.size()) ? m_style.damageTargetX.at(i)
                                                                     : fallbackX;
        const QPoint end(qRound(targetCenterX - fly->width() / 2.0), qRound(m_style.damageTargetY));

        auto *move = new QPropertyAnimation(fly, "pos", fly);
        move->setDuration(kDamageFlyMs);
        move->setStartValue(start);
        move->setEndValue(end);
        move->setEasingCurve(QEasingCurve::InCubic);

        auto *fade = new QPropertyAnimation(effect, "opacity", fly);
        fade->setDuration(kDamageFlyMs);
        fade->setStartValue(1.0);
        fade->setEndValue(0.0);
        fade->setEasingCurve(QEasingCurve::InQuad);

        move->start(QAbstractAnimation::DeleteWhenStopped);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
        connect(move, &QPropertyAnimation::finished, fly, [this, fly]() {
            m_flyLabels.removeAll(fly);
            fly->deleteLater();
        });
    }

    // 扣血数字落到血条上（起飞 780ms 后）才开始掉血：
    // 血条动画从这一刻起跑，而不是和数字同时起跑。
    QTimer::singleShot(kDamageFlyMs, this, [this]() {
        if (!m_open || !m_multiplayerReview || m_scoreCards.isEmpty())
            return;
        emit damagePhaseStarted();
    });
}

void MapOverlay::updateLayout()
{
    // 打开动画期间按「最终尺寸」布局，并把地图按最终中心对齐：
    // 否则小窗只露出地图左上角，看起来像"先放大、再变回全图"。
    const bool opening = m_animating && !m_closing && m_targetRect.isValid();
    const QRect layoutRect = opening ? m_targetRect : rect();

    const int w = layoutRect.width();
    const int h = layoutRect.height();
    if (w <= 0 || h <= 0)
        return;

    const int margin = qBound(10, qMin(w, h) / 24, 40);
    const int barHeight = qBound(48, h / 9, 76);
    const int mapHeight = qMax(80, h - margin * 2 - barHeight - 10);

    const int offsetX = opening ? (m_targetRect.width() - width()) / 2 : 0;
    const int offsetY = opening ? (m_targetRect.height() - height()) / 2 : 0;

    m_mapView->setGeometry(layoutRect.x() + margin - offsetX,
                           layoutRect.y() + margin - offsetY,
                           qMax(80, w - margin * 2), mapHeight);
    m_bottomBar->setGeometry(layoutRect.x() + margin - offsetX,
                             layoutRect.y() + h - margin - barHeight - offsetY,
                             qMax(80, w - margin * 2), barHeight);

    // 分数框：有 cardRects 时一格对一格摆在各自血条下方，否则退回原来居中的一排。
    if (m_scoreStrip) {
        if (!m_scoreCards.isEmpty() && m_style.cardRects.size() == m_scoreCards.size()) {
            layoutScoreCards();
        } else {
            const int stripWidth = qMin(760, qMax(320, w - 200));
            m_scoreStrip->setGeometry(layoutRect.x() + (w - stripWidth) / 2 - offsetX,
                                      layoutRect.y() + 150 - offsetY,
                                      stripWidth, 76);
        }
    }

    m_mapView->raise();
    m_bottomBar->raise();
    if (m_scoreStrip && m_scoreStrip->isVisible())
        m_scoreStrip->raise();
    if (m_scoreLabel->isVisible())
        m_scoreLabel->move((width() - m_scoreLabel->width()) / 2, 36);
}
