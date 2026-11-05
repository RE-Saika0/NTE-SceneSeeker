#include "BgmPlayer.h"

#include "AssetPaths.h"

#include <QAudioOutput>
#include <QDir>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QRandomGenerator>
#include <QVariantAnimation>

namespace {
const QStringList kAudioFilters{
    QStringLiteral("*.mp3"), QStringLiteral("*.ogg"), QStringLiteral("*.wav"),
    QStringLiteral("*.flac"), QStringLiteral("*.m4a"),
};
constexpr int kFadeMs = 900;
}

BgmPlayer::BgmPlayer(QObject *parent)
    : QObject(parent)
{
    m_player = new QMediaPlayer(this);
    m_output = new QAudioOutput(this);
    m_player->setAudioOutput(m_output);
    applyVolume();

    m_fade = new QVariantAnimation(this);
    m_fade->setDuration(kFadeMs);
    connect(m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_fadeLevel = value.toReal();
        applyVolume();
    });

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            advance();
        } else if (status == QMediaPlayer::InvalidMedia) {
            qWarning().noquote() << "BGM 无法播放:" << currentTrack();
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        qWarning().noquote() << "BGM 错误:" << message;
    });
}

void BgmPlayer::reloadTracks()
{
    m_tracks.clear();
    m_order.clear();
    m_pos = 0;

    const QDir dir(AssetPaths::assets(QStringLiteral("audio/bgm")));
    if (dir.exists()) {
        const QFileInfoList entries = dir.entryInfoList(kAudioFilters, QDir::Files, QDir::Name);
        for (const QFileInfo &info : entries)
            m_tracks.append(info.absoluteFilePath());
    }

    if (m_tracks.isEmpty()) {
        // 退回单文件形式
        const QString single = AssetPaths::assets(QStringLiteral("audio/bgm_menu.mp3"));
        if (QFileInfo::exists(single))
            m_tracks.append(single);
    }

    // 随机顺序：洗牌一次，播完一整轮再重洗，避免"总在同一首之后"
    m_order.reserve(m_tracks.size());
    for (int i = 0; i < m_tracks.size(); ++i)
        m_order.append(i);
    for (int i = m_order.size() - 1; i > 0; --i) {
        const int j = QRandomGenerator::global()->bounded(i + 1);
        std::swap(m_order[i], m_order[j]);
    }

    if (m_tracks.isEmpty())
        qInfo().noquote() << "没有找到 BGM（放 assets/audio/bgm/*.mp3 或 assets/audio/bgm_menu.mp3）";
    else
        qInfo().noquote() << "BGM 已发现" << m_tracks.size() << "首，随机顺序播放";
}

void BgmPlayer::advance()
{
    if (m_tracks.isEmpty())
        return;
    ++m_pos;
    if (m_pos >= m_order.size()) {
        // 一轮播完，重新洗牌
        for (int i = m_order.size() - 1; i > 0; --i) {
            const int j = QRandomGenerator::global()->bounded(i + 1);
            std::swap(m_order[i], m_order[j]);
        }
        m_pos = 0;
    }
    playIndex(m_order.at(m_pos));
}

QString BgmPlayer::currentTrack() const
{
    if (m_index < 0 || m_index >= m_tracks.size())
        return QString();
    return QFileInfo(m_tracks.at(m_index)).fileName();
}

void BgmPlayer::start()
{
    if (m_started)
        return;
    reloadTracks();
    if (m_tracks.isEmpty())
        return;
    m_started = true;
    m_pos = 0;
    playIndex(m_order.isEmpty() ? 0 : m_order.at(0));
}

void BgmPlayer::stop()
{
    m_started = false;
    if (m_player)
        m_player->stop();
}

void BgmPlayer::playIndex(int index)
{
    if (m_tracks.isEmpty())
        return;
    m_index = qBound(0, index, m_tracks.size() - 1);

    m_player->stop();
    m_player->setSource(QUrl::fromLocalFile(m_tracks.at(m_index)));
    m_player->setLoops(1); // 单曲不循环，靠 EndOfMedia 切下一首

    fade(0.0, 1.0, kFadeMs);
    m_player->play();
    emit trackChanged(currentTrack());
    qInfo().noquote() << "BGM 播放:" << currentTrack();
}

void BgmPlayer::fade(qreal from, qreal to, int durationMs)
{
    m_fade->stop();
    m_fade->setDuration(durationMs);
    m_fade->setStartValue(from);
    m_fade->setEndValue(to);
    m_fade->start();
}

void BgmPlayer::setVolume(qreal volume)
{
    m_master = qBound(0.0, volume, 1.0);
    applyVolume();
}

void BgmPlayer::applyVolume()
{
    if (m_output)
        m_output->setVolume(qBound(0.0, m_master * m_fadeLevel, 1.0));
}
