#include "BackgroundCarousel.h"

#include "AssetPaths.h"
#include "bank/QuestionBank.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
#include <QRandomGenerator>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QTimer>
#include <QVariantAnimation>

#include <algorithm>
#include <random>

namespace {
constexpr qreal kMaxZoom = 0.10; // 10% slow push-in
}

BackgroundCarousel::BackgroundCarousel(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_tick = new QTimer(this);
    m_tick->setInterval(33);
    connect(m_tick, &QTimer::timeout, this, [this]() {
        const qreal span = qMax(1, m_intervalMs);
        m_zoom = qBound<qreal>(0.0, m_zoom + 33.0 / span, 1.0);
        update();
        if (m_slideClock.elapsed() >= m_intervalMs && m_slides.size() > 1) {
            advance();
        }
    });

    m_fadeAnimation = new QVariantAnimation(this);
    m_fadeAnimation->setDuration(m_transitionMs);
    m_fadeAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_fadeAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_fade = value.toReal();
        if (m_fade >= 1.0)
            m_prevIndex = -1;
        update();
    });
}

void BackgroundCarousel::loadFromQuestionBank(QuestionBank *bank, int maxSlides)
{
    m_bank = bank;
    m_slides.clear();
    m_index = 0;
    m_prevIndex = -1;

    if (bank && maxSlides > 0) {
        QVector<BankQuestion> candidates;
        for (const BankQuestion &question : bank->approvedQuestions()) {
            if (question.type != QStringLiteral("photo") || question.mediaId.isEmpty())
                continue;
            // 主菜单会显示“城市 · 地区”；没有区域信息的题跳过，避免回退成
            // media_id 那种数字/字母串。
            if (question.district.trimmed().isEmpty() && question.area.trimmed().isEmpty()
                && question.title.trimmed().isEmpty()) {
                continue;
            }
            candidates.append(question);
        }

        std::mt19937 rng(QRandomGenerator::global()->generate());
        std::shuffle(candidates.begin(), candidates.end(), rng);

        for (int i = 0; i < candidates.size() && m_slides.size() < maxSlides; ++i) {
            const BankQuestion &question = candidates.at(i);
            Slide slide;
            slide.mediaId = question.mediaId;
            slide.district = question.district;
            slide.area = question.area;
            if (!question.district.isEmpty() && !question.area.isEmpty())
                slide.caption = QStringLiteral("%1 · %2").arg(question.district, question.area);
            else if (!question.area.isEmpty())
                slide.caption = question.area;
            else if (!question.district.isEmpty())
                slide.caption = question.district;
            else if (!question.title.trimmed().isEmpty())
                slide.caption = question.title;
            m_slides.append(slide);
        }
    }

    m_fade = 1.0;
    m_zoom = 0.0;
    if (!m_slides.isEmpty()) {
        ensureScaled(0);
        emit slideChanged(0, currentCaption());
    }
    update();
}

void BackgroundCarousel::loadFromManifest(const QString &manifestPath)
{
    m_bank = nullptr;
    m_slides.clear();
    m_index = 0;
    m_prevIndex = -1;

    QFile file(manifestPath);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        const QJsonObject root = document.object();
        if (root.contains(QStringLiteral("intervalMs")))
            m_intervalMs = qMax(3000, root.value(QStringLiteral("intervalMs")).toInt(m_intervalMs));
        if (root.contains(QStringLiteral("transitionMs")))
            m_transitionMs = qMax(200, root.value(QStringLiteral("transitionMs")).toInt(m_transitionMs));
        if (root.contains(QStringLiteral("dim")))
            m_dim = qBound<qreal>(0.0, root.value(QStringLiteral("dim")).toDouble(m_dim), 1.0);

        const QJsonArray items = root.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject object = value.toObject();
            if (object.contains(QStringLiteral("enabled"))
                && !object.value(QStringLiteral("enabled")).toBool(true)) {
                continue;
            }
            Slide slide;
            slide.imagePath = object.value(QStringLiteral("image")).toString();
            slide.district = object.value(QStringLiteral("district")).toString();
            slide.area = object.value(QStringLiteral("area")).toString();
            slide.caption = object.value(QStringLiteral("caption")).toString();
            if (!slide.imagePath.isEmpty())
                m_slides.append(slide);
        }
    }

    // 没有清单时退回扫描目录，方便直接丢图片进去就能用。
    if (m_slides.isEmpty()) {
        const QDir dir(AssetPaths::assets(QStringLiteral("ui/backgrounds")));
        const QStringList filters{QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                  QStringLiteral("*.png"), QStringLiteral("*.webp")};
        for (const QString &name : dir.entryList(filters, QDir::Files, QDir::Name))
            m_slides.append(Slide{QStringLiteral("ui/backgrounds/") + name, {}, {}, {}, {}, {}});
    }

    m_fade = 1.0;
    m_zoom = 0.0;
    if (!m_slides.isEmpty()) {
        ensureScaled(0);
        emit slideChanged(0, currentCaption());
    }
    update();
}

QString BackgroundCarousel::captionAt(int index) const
{
    if (index < 0 || index >= m_slides.size())
        return QString();
    const Slide &slide = m_slides.at(index);
    if (!slide.caption.isEmpty())
        return slide.caption;
    if (!slide.district.isEmpty() && !slide.area.isEmpty())
        return QStringLiteral("%1 · %2").arg(slide.district, slide.area);
    if (!slide.area.isEmpty())
        return slide.area;
    if (!slide.district.isEmpty())
        return slide.district;
    if (!slide.mediaId.isEmpty())
        return QStringLiteral("未知地点");
    return QString::fromLocal8Bit(QFileInfo(slide.imagePath).completeBaseName().toUtf8());
}

QString BackgroundCarousel::currentCaption() const
{
    return captionAt(m_index);
}

void BackgroundCarousel::ensureScaled(int index)
{
    if (index < 0 || index >= m_slides.size() || width() <= 0 || height() <= 0)
        return;

    Slide &slide = m_slides[index];
    const qreal dpr = devicePixelRatioF();
    const QSize target(qMax(1, qRound(width() * dpr * 1.12)),
                       qMax(1, qRound(height() * dpr * 1.12)));

    QImage image;
    if (!slide.mediaId.isEmpty()) {
        if (m_bank)
            image.loadFromData(m_bank->mediaBytes(slide.mediaId));
    } else {
        image = QImage(AssetPaths::questionAsset(slide.imagePath));
    }
    if (image.isNull())
        return;

    // 只保留显示器够用的分辨率，避免 4K 原图每帧重采样。
    if (image.width() > target.width() * 2 || image.height() > target.height() * 2)
        image = image.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

    slide.scaled = QPixmap::fromImage(image.scaled(target, Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation));
    slide.scaled.setDevicePixelRatio(dpr);
}

void BackgroundCarousel::start()
{
    if (m_slides.isEmpty())
        return;
    m_slideClock.start();
    m_tick->start();
}

void BackgroundCarousel::stop()
{
    m_tick->stop();
    m_fadeAnimation->stop();
}

void BackgroundCarousel::setDim(qreal dim)
{
    m_dim = qBound<qreal>(0.0, dim, 1.0);
    update();
}

void BackgroundCarousel::next()
{
    if (m_slides.size() <= 1)
        return;
    advance();
}

void BackgroundCarousel::advance()
{
    showSlide((m_index + 1) % m_slides.size(), true);
}

void BackgroundCarousel::showSlide(int index, bool animate)
{
    if (index < 0 || index >= m_slides.size() || index == m_index)
        return;

    ensureScaled(index);
    m_prevIndex = m_index;
    m_prevZoom = m_zoom;
    m_index = index;
    m_zoom = 0.0;
    m_slideClock.restart();
    emit slideChanged(m_index, currentCaption());

    if (!animate) {
        m_fade = 1.0;
        m_prevIndex = -1;
        update();
        return;
    }

    m_fadeAnimation->stop();
    m_fade = 0.0;
    m_fadeAnimation->setDuration(m_transitionMs);
    m_fadeAnimation->setStartValue(0.0);
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
}

void BackgroundCarousel::drawSlide(QPainter &painter, const Slide &slide, qreal zoom, qreal opacity)
{
    if (slide.scaled.isNull())
        return;

    painter.save();
    painter.setOpacity(opacity);

    // 慢推拉：画面缓慢放大，并按奇偶做极小的横向漂移，避免"死图"感。
    const qreal grow = 1.0 + kMaxZoom * zoom;
    const qreal drift = (m_index % 2 == 0 ? 1.0 : -1.0) * 10.0 * zoom;
    QSizeF size(width() * grow, height() * grow);
    const QRectF target((width() - size.width()) / 2.0 + drift,
                        (height() - size.height()) / 2.0,
                        size.width(), size.height());
    painter.drawPixmap(target, slide.scaled, QRectF(slide.scaled.rect()));
    painter.restore();
}

void BackgroundCarousel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_slides.isEmpty()) {
        QLinearGradient fallback(0, 0, width(), height());
        fallback.setColorAt(0.0, QColor(QStringLiteral("#0B1220")));
        fallback.setColorAt(0.6, QColor(QStringLiteral("#0E1A2B")));
        fallback.setColorAt(1.0, QColor(QStringLiteral("#05070A")));
        painter.fillRect(rect(), fallback);
        return;
    }

    painter.fillRect(rect(), QColor(QStringLiteral("#05070A")));

    if (m_prevIndex >= 0 && m_prevIndex < m_slides.size() && m_fade < 1.0)
        drawSlide(painter, m_slides.at(m_prevIndex), m_prevZoom, 1.0);
    drawSlide(painter, m_slides.at(m_index), m_zoom, m_fade < 1.0 ? m_fade : 1.0);

    // 全局压暗：主菜单较轻，进入设置/房间等子页面时调高。
    painter.fillRect(rect(), QColor(3, 6, 11, int(160 * m_dim)));

    // 左侧压暗，保证标题压在大图上依然能读。
    QLinearGradient leftScrim(0, 0, width() * 0.72, 0);
    leftScrim.setColorAt(0.0, QColor(4, 8, 14, 205));
    leftScrim.setColorAt(0.32, QColor(4, 8, 14, 110));
    leftScrim.setColorAt(1.0, QColor(4, 8, 14, 0));
    painter.fillRect(rect(), leftScrim);

    // 底部渐深：标题/导航永远落在暗部。
    QLinearGradient vertical(0, 0, 0, height());
    vertical.setColorAt(0.0, QColor(3, 6, 11, 150));
    vertical.setColorAt(0.15, QColor(3, 6, 11, 0));
    vertical.setColorAt(0.42, QColor(3, 6, 11, 10));
    vertical.setColorAt(0.62, QColor(3, 6, 11, 95));
    vertical.setColorAt(0.80, QColor(3, 6, 11, 185));
    vertical.setColorAt(1.0, QColor(3, 6, 11, 238));
    painter.fillRect(rect(), vertical);

    // 暗角。
    QRadialGradient vignette(rect().center(), qMax(width(), height()) * 0.72);
    vignette.setColorAt(0.55, QColor(0, 0, 0, 0));
    vignette.setColorAt(1.0, QColor(0, 0, 0, int(130 * m_dim)));
    painter.fillRect(rect(), vignette);

    // 杂志内框，给画面一点"印刷品"的边界感。
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 255, 255, 26), 1));
    painter.drawRect(rect().adjusted(26, 26, -27, -27));
}

void BackgroundCarousel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    for (Slide &slide : m_slides)
        slide.scaled = QPixmap();
    ensureScaled(m_index);
    if (m_prevIndex >= 0)
        ensureScaled(m_prevIndex);
}
