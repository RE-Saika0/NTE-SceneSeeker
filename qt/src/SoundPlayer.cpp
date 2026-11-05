#include "SoundPlayer.h"

#include "AssetPaths.h"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QHash>
#include <QMouseEvent>
#include <QPushButton>
#include <QSoundEffect>
#include <QUrl>

namespace {

class Player : public QObject
{
public:
    void init()
    {
        qApp->installEventFilter(this);

        const QStringList names{
            QStringLiteral("ui_click"), QStringLiteral("ui_hover"),
            QStringLiteral("ui_confirm"), QStringLiteral("ui_back"),
            QStringLiteral("round_start"), QStringLiteral("countdown_tick"),
            QStringLiteral("countdown_go"), QStringLiteral("submit"),
            QStringLiteral("score_tick"), QStringLiteral("score_hit"),
            QStringLiteral("damage"), QStringLiteral("match_end"),
        };
        for (const QString &name : names) {
            const QString path = AssetPaths::assets(QStringLiteral("audio/sfx/") + name
                                                    + QStringLiteral(".wav"));
            if (!QFileInfo::exists(path))
                continue;
            auto *effect = new QSoundEffect(this);
            effect->setSource(QUrl::fromLocalFile(path));
            effect->setLoopCount(1);
            effect->setVolume(m_volume);
            m_effects.insert(name, effect);
        }
        qInfo().noquote() << "音效已加载" << m_effects.size() << "个，目录"
                          << AssetPaths::assets(QStringLiteral("audio/sfx"));
    }

    void play(const QString &name, qreal gain)
    {
        QSoundEffect *effect = m_effects.value(name, nullptr);
        if (!effect)
            return;

        // 同一个音效 40ms 内不重复触发，避免连点叠成噪音。
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_last.value(name, 0) < 40)
            return;
        m_last.insert(name, now);

        effect->setVolume(qBound(0.0, m_volume * gain, 1.0));
        effect->play();
    }

    void setVolume(qreal volume)
    {
        m_volume = qBound(0.0, volume, 1.0);
        for (QSoundEffect *effect : m_effects)
            effect->setVolume(m_volume);
    }

    qreal volume() const { return m_volume; }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        auto *button = qobject_cast<QPushButton *>(object);
        if (button && button->isEnabled() && button->isVisible()) {
            if (event->type() == QEvent::Enter) {
                play(QStringLiteral("ui_hover"), 0.45);
            } else if (event->type() == QEvent::MouseButtonRelease) {
                auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() == Qt::LeftButton)
                    play(QStringLiteral("ui_click"), 0.9);
            }
        }
        return QObject::eventFilter(object, event);
    }

private:
    QHash<QString, QSoundEffect *> m_effects;
    QHash<QString, qint64> m_last;
    qreal m_volume = 0.7;
};

Player *g_player = nullptr;

} // namespace

namespace Sfx {

void init()
{
    if (g_player)
        return;
    g_player = new Player;
    g_player->init();
}

void play(const QString &name, qreal gain)
{
    if (g_player)
        g_player->play(name, gain);
}

void setVolume(qreal volume)
{
    if (g_player)
        g_player->setVolume(volume);
}

qreal volume()
{
    return g_player ? g_player->volume() : 0.7;
}

} // namespace Sfx
