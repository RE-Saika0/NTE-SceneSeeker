#pragma once

#include <QString>
#include <QWidget>

// 主菜单标题的"艺术字"：书法字体 + 渐变填充 + 柔和描边与外发光。
//
// 不依赖额外资源文件，优先使用系统里的华文行楷 / 华文隶书 / 隶书，
// 找不到时回退到微软雅黑，仍然保留渐变与描边效果。
class CoverTitleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CoverTitleWidget(const QString &text, QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
    void setText(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString resolveFamily() const;

    QString m_text;
    QString m_family;
    int m_pixelSize = 132;
};
