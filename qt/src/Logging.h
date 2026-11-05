#pragma once

#include <QString>

// 运行日志：写到 %APPDATA%/NTE SceneSeeker/NTE-SceneSeeker.log，同时镜像到 stderr。
// 超过 2 MB 自动轮转为 .1，避免无限增长。
namespace Logging {

void install();
QString filePath();

} // namespace Logging
