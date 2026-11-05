#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>

class QMediaPlayer;
class QAudioOutput;
class QVariantAnimation;

// 背景音乐：扫描 assets/audio/bgm/ 下的音频，顺序循环播放，
// 切歌时淡出→淡入，避免硬切。
// 若目录为空，会退回单文件 assets/audio/bgm_menu.mp3。
class BgmPlayer : public QObject
{
    Q_OBJECT

public:
    explicit BgmPlayer(QObject *parent = nullptr);

    void start();
    void stop();
    void setVolume(qreal volume); // 0.0 ~ 1.0
    qreal volume() const { return m_master; }
    int trackCount() const { return m_tracks.size(); }
    QString currentTrack() const;

signals:
    void trackChanged(const QString &name);

private:
    void reloadTracks();
    void advance();
    void playIndex(int index);
    void fade(qreal from, qreal to, int durationMs);
    void applyVolume();

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_output = nullptr;
    QVariantAnimation *m_fade = nullptr;

    QStringList m_tracks;
    QVector<int> m_order;
    int m_pos = 0;
    int m_index = 0;
    qreal m_master = 0.35;
    qreal m_fadeLevel = 1.0;
    bool m_started = false;
};
