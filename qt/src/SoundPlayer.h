#pragma once

#include <QString>

// 合成音效播放器：预加载 assets/audio/sfx/*.wav，全局按钮点击/悬停自动出声。
// 用单例是为了让 MapOverlay 之类的小部件不必层层传递指针。
namespace Sfx {

void init();
void play(const QString &name, qreal gain = 1.0);
void setVolume(qreal volume);   // 0.0 ~ 1.0
qreal volume();

} // namespace Sfx
