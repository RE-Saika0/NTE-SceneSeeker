#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>

class QTimer;
class QVariantAnimation;
class QuestionBank;

// 全屏背景轮播：杂志封面式的大图慢推拉 + 交叉淡入。
// 图片来自题库 media.ntep 里的 approved photo 题，不再需要单独的背景图文件夹。
class BackgroundCarousel : public QWidget
{
    Q_OBJECT

public:
    explicit BackgroundCarousel(QWidget *parent = nullptr);

    void loadFromQuestionBank(QuestionBank *bank, int maxSlides = 12);
    void loadFromManifest(const QString &manifestPath);
    void start();
    void stop();
    void setDim(qreal dim);
    void next();

    int count() const { return m_slides.size(); }
    int currentIndex() const { return m_index; }
    QString currentCaption() const;
    QString captionAt(int index) const;

signals:
    void slideChanged(int index, const QString &caption);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Slide {
        QString imagePath;
        QString mediaId;
        QString district;
        QString area;
        QString caption;
        QPixmap scaled;
    };

    void loadSlides();
    void ensureScaled(int index);
    void showSlide(int index, bool animate);
    void advance();
    void drawSlide(QPainter &painter, const Slide &slide, qreal zoom, qreal opacity);

    QVector<Slide> m_slides;
    QuestionBank *m_bank = nullptr;
    int m_index = 0;
    int m_prevIndex = -1;
    int m_intervalMs = 9000;
    int m_transitionMs = 1400;
    qreal m_dim = 0.55;
    qreal m_fade = 1.0;
    qreal m_zoom = 0.0;
    qreal m_prevZoom = 0.0;

    QTimer *m_tick = nullptr;
    QVariantAnimation *m_fadeAnimation = nullptr;
    QElapsedTimer m_slideClock;
};
