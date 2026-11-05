#include "Logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>
#include <QSysInfo>
#include <QtGlobal>

#include <cstdio>

namespace {

QString g_path;
QMutex g_mutex;
constexpr qint64 kMaxBytes = 2 * 1024 * 1024;

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO ";
    case QtWarningMsg:
        return "WARN ";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }
    return "INFO ";
}

void rotateIfNeeded()
{
    QFileInfo info(g_path);
    if (!info.exists() || info.size() < kMaxBytes)
        return;
    const QString backup = g_path + QStringLiteral(".1");
    QFile::remove(backup);
    QFile::rename(g_path, backup);
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QString line = QStringLiteral("%1 [%2] %3")
                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                            QString::fromLatin1(levelName(type)),
                            message);
    if (context.category && qstrcmp(context.category, "default") != 0)
        line += QStringLiteral("  (%1)").arg(QString::fromLatin1(context.category));
    line += QLatin1Char('\n');

    {
        QMutexLocker locker(&g_mutex);
        rotateIfNeeded();
        QFile file(g_path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append))
            file.write(line.toUtf8());
    }

    // 仍然输出到 stderr，方便从命令行启动时直接看。
    std::fputs(line.toLocal8Bit().constData(), stderr);
    std::fflush(stderr);

    if (type == QtFatalMsg)
        std::abort();
}

} // namespace

namespace Logging {

QString filePath()
{
    return g_path;
}

void install()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dir)) {
        g_path = QDir::temp().filePath(QStringLiteral("NTE-SceneSeeker.log"));
    } else {
        g_path = QDir(dir).filePath(QStringLiteral("NTE-SceneSeeker.log"));
    }

    qInstallMessageHandler(messageHandler);

    qInfo().noquote() << "=== NTE SceneSeeker 启动 ===";
    qInfo().noquote() << "版本" << QCoreApplication::applicationVersion()
                      << "· Qt" << qVersion()
                      << "·" << QSysInfo::prettyProductName()
                      << "·" << QSysInfo::currentCpuArchitecture();
    qInfo().noquote() << "日志文件:" << g_path;
    qInfo().noquote() << "参数:" << QCoreApplication::arguments().join(QLatin1Char(' '));
}

} // namespace Logging
