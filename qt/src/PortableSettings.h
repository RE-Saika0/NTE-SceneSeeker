#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QString>

// 绿色版设置：所有存档/设置（窗口状态、音量、上次的房间地址与名字、clientId…）
// 都写在 exe 同目录的 INI 文件里，不再是注册表 / %APPDATA%。
//
// 同一台电脑多开时用 --profile 区分：NTE-SceneSeeker-<profile>.ini，
// 这样几个实例不会共用同一个 clientId / 名字。
namespace PortableSettings {

// main() 里解析 --profile 后立刻设置（必须在第一次 appSettings() 之前）。
inline QString &profileSuffix()
{
    static QString suffix;
    return suffix;
}

inline QString filePath()
{
    QString name = QStringLiteral("NTE-SceneSeeker");
    if (!profileSuffix().isEmpty())
        name += QLatin1Char('-') + profileSuffix();
    name += QStringLiteral(".ini");
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

inline QSettings *appSettings()
{
    static QSettings *instance = new QSettings(filePath(), QSettings::IniFormat);
    return instance;
}

} // namespace PortableSettings
