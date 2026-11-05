#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

// 所有资源路径都从 applicationDirPath() 出发；开发时 exe 在 build/qt，
// 会向上查找若干级，找到项目根目录的 assets/。发布时把 assets/ 放到 exe 旁边即可。
namespace AssetPaths {

inline QString fromAppDir(const QString &relative)
{
    QString rel = relative;
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);

    QDir dir(QCoreApplication::applicationDirPath());
    const QString first = dir.filePath(rel);
    if (QFileInfo::exists(first))
        return first;

    // 允许从 build/qt 或 build/qt/Debug 等目录向上寻找 assets/。
    for (int i = 0; i < 6; ++i) {
        if (!dir.cdUp())
            break;
        const QString candidate = dir.filePath(rel);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return first;
}

inline QString assets(const QString &relative)
{
    return fromAppDir(QStringLiteral("assets/") + relative);
}

inline QString mapTilesDir()
{
    return assets(QStringLiteral("map/tiles"));
}

// 地图标签（路名/地名），随 assets/data 一起发布。
inline QString mapLabels()
{
    const QString direct = assets(QStringLiteral("data/map_labels.json"));
    if (QFileInfo::exists(direct))
        return direct;
    // 开发期回退：项目根的 data/map_labels.json
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        const QString candidate = dir.filePath(QStringLiteral("data/map_labels.json"));
        if (QFileInfo::exists(candidate))
            return candidate;
        if (!dir.cdUp())
            break;
    }
    return direct;
}

inline bool mapTilesAvailable()
{
    return QFileInfo::exists(assets(QStringLiteral("map/tiles/6/0_0.jpg")))
        || QFileInfo::exists(assets(QStringLiteral("map/tiles/0/0_0.jpg")));
}

inline QString questionAsset(const QString &relative)
{
    QString rel = relative;
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (rel.startsWith(QStringLiteral("assets/")))
        return fromAppDir(rel);

    const QString direct = assets(rel);
    if (QFileInfo::exists(direct))
        return direct;

    // 开发期兼容旧采集目录：早期标注器把图片放在 temp/mapgame。
    // 发布包应统一把图片放进 assets/，不依赖这个回退路径。
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        const QString candidate =
            dir.filePath(QStringLiteral("temp/mapgame/") + rel);
        if (QFileInfo::exists(candidate))
            return candidate;
        if (!dir.cdUp())
            break;
    }
    return direct;
}

inline QString questionImage(const QString &image)
{
    return questionAsset(image);
}

} // namespace AssetPaths
