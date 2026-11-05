#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>

// 题库图片包（media.ntep）：只支持 ZIP 的 store（不压缩）模式。
// 图片本来就已经压缩过，store 让读写实现简单、不依赖 zlib，7-Zip/资源管理器仍能打开。
class MediaPackWriter
{
public:
    explicit MediaPackWriter(const QString &path);
    ~MediaPackWriter();

    bool addFile(const QString &entryName, const QString &sourcePath, QString *error = nullptr);
    bool addBytes(const QString &entryName, const QByteArray &bytes, QString *error = nullptr);
    bool finish(QString *error = nullptr);

    static quint32 crc32(const QByteArray &data);
    static quint32 crc32Update(quint32 crc, const char *data, qint64 len);

private:
    struct CentralEntry {
        QByteArray name;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
        quint16 dosTime = 0;
        quint16 dosDate = 0;
    };

    bool writeLocal(const QByteArray &name, const QByteArray &bytes, QString *error);
    static void dosDateTime(quint16 *dosTime, quint16 *dosDate);

    QFile m_file;
    QVector<CentralEntry> m_entries;
    bool m_finished = false;
};

class MediaPackReader
{
public:
    bool open(const QString &path, QString *error = nullptr);
    void close();
    bool isOpen() const { return !m_entries.isEmpty() || m_opened; }
    QStringList entries() const { return m_entries.keys(); }
    bool contains(const QString &name) const { return m_entries.contains(name); }
    qint64 entrySize(const QString &name) const;
    // 读出一个条目的原始字节；不存在/损坏返回空。
    QByteArray read(const QString &name);

private:
    struct Entry {
        quint16 method = 0;
        quint32 crc = 0;
        quint32 compressedSize = 0;
        quint32 size = 0;
        quint32 localOffset = 0;
        bool valid = false;
    };

    QFile m_file;
    QHash<QString, Entry> m_entries;
    bool m_opened = false;
};
