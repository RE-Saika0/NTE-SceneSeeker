#include "CoverTitleWidget.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace {
// 现代无衬线优先：粗黑体做字标，避免书法字体带来的"节庆/民俗"气质。
const QStringList kPreferredFamilies{
    QStringLiteral("Noto Sans SC Black"),
    QStringLiteral("HarmonyOS Sans SC"),
    QStringLiteral("Noto Sans SC"),
    QStringLiteral("Microsoft YaHei UI"),
    QStringLiteral("Microsoft YaHei"),
};
}

CoverTitleWidget::CoverTitleWidget(const QString &text, QWidget *parent)
    : QWidget(parent)
    , m_text(text)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_family = resolveFamily();
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QString CoverTitleWidget::resolveFamily() const
{
    const QStringList families = QFontDatabase::families();
    for (const QString &candidate : kPreferredFamilies) {
        if (families.contains(candidate, Qt::CaseInsensitive))
            return candidate;
    }
    return QStringLiteral("Microsoft YaHei UI");
}

void CoverTitleWidget::setText(const QString &text)
{
    m_text = text;
    updateGeometry();
    update();
}

QSize CoverTitleWidget::sizeHint() const
{
    QFont font(m_family);
    font.setPixelSize(m_pixelSize);
    font.setWeight(QFont::Black);
    font.setLetterSpacing(QFont::AbsoluteSpacing, m_pixelSize * -0.012);
    const QFontMetricsF metrics(font);
    const qreal square = m_pixelSize * 0.11;
    return QSize(qCeil(metrics.horizontalAdvance(m_text) + m_pixelSize * 0.18 + square),
                 qCeil(m_pixelSize * 1.34));
}

void CoverTitleWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (m_text.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font(m_family);
    font.setPixelSize(m_pixelSize);
    font.setWeight(QFont::Black);
    font.setLetterSpacing(QFont::AbsoluteSpacing, m_pixelSize * -0.012);
    const QFontMetricsF metrics(font);

    // 逐字排布，保留极小的基线错位，像手工排版而不是系统默认控件。
    static const qreal kOffsets[4] = {0.0, -0.008, 0.006, -0.006};
    QPainterPath path;
    qreal x = 0.0;
    const qreal baseline = (height() + metrics.ascent() - metrics.descent()) * 0.5;
    const int count = m_text.size();
    for (int i = 0; i < count; ++i) {
        const QString glyph = m_text.mid(i, 1);
        const qreal dy = (count <= 4 ? kOffsets[i % 4] : 0.0) * m_pixelSize;
        path.addText(x, baseline + dy, font, glyph);
        x += metrics.horizontalAdvance(glyph);
    }

    const QRectF bounds = path.boundingRect();
    painter.save();
    if (bounds.width() > width() - m_pixelSize * 0.30 && bounds.width() > 0) {
        const qreal scale = (width() - m_pixelSize * 0.30) / bounds.width();
        painter.scale(scale, scale);
    }

    // 柔和落影：用几层宽描边近似模糊，比单层硬阴影更干净。
    painter.setBrush(Qt::NoBrush);
    auto shadowStroke = [&painter, &path](const QColor &color, qreal width) {
        QPen pen(color, width);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawPath(path);
    };
    painter.save();
    painter.translate(0, m_pixelSize * 0.022);
    shadowStroke(QColor(0, 0, 0, 18), m_pixelSize * 0.065);
    shadowStroke(QColor(0, 0, 0, 28), m_pixelSize * 0.035);
    painter.restore();

    // 字面：接近白，但压到 #EFF5F9，不会在亮背景上"发光"。
    QLinearGradient fill(bounds.topLeft(), bounds.bottomLeft());
    fill.setColorAt(0.0, QColor(QStringLiteral("#FBFDFE")));
    fill.setColorAt(0.55, QColor(QStringLiteral("#E6EEF4")));
    fill.setColorAt(1.0, QColor(QStringLiteral("#C3D3DE")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawPath(path);

    // 极细深色边，压住浅色天空，轮廓更利落。
    QPen outline(QColor(10, 28, 38, 90), 1.0);
    outline.setJoinStyle(Qt::RoundJoin);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    // 青色小方块作为唯一强调色，替代花哨描边/发光。
    const qreal square = m_pixelSize * 0.11;
    const qreal squareX = qMax(bounds.right() + m_pixelSize * 0.16, x + m_pixelSize * 0.06);
    const QRectF accent(squareX, baseline - square, square, square);
    QLinearGradient accentFill(accent.topLeft(), accent.bottomRight());
    accentFill.setColorAt(0.0, QColor(QStringLiteral("#67E8F9")));
    accentFill.setColorAt(1.0, QColor(QStringLiteral("#0EA5E9")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(accentFill);
    painter.drawRect(accent);

    painter.restore();
}
