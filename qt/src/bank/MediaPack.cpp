#include "MediaPack.h"

#include <QDataStream>
#include <QDateTime>
#include <QFileInfo>

namespace {

constexpr quint32 kLocalSig = 0x04034b50;
constexpr quint32 kCentralSig = 0x02014b50;
constexpr quint32 kEndSig = 0x06054b50;

quint32 crcTable[256];
bool crcTableReady = false;

void ensureCrcTable()
{
    if (crcTableReady)
        return;
    for (quint32 i = 0; i < 256; ++i) {
        quint32 c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crcTable[i] = c;
    }
    crcTableReady = true;
}

} // namespace

quint32 MediaPackWriter::crc32Update(quint32 crc, const char *data, qint64 len)
{
    ensureCrcTable();
    crc ^= 0xFFFFFFFFu;
    for (qint64 i = 0; i < len; ++i)
        crc = crcTable[(crc ^ quint8(data[i])) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

quint32 MediaPackWriter::crc32(const QByteArray &data)
{
    return crc32Update(0, data.constData(), data.size());
}

void MediaPackWriter::dosDateTime(quint16 *dosTime, quint16 *dosDate)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date();
    const QTime t = now.time();
    *dosTime = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
    *dosDate = quint16(((d.year() - 1980) << 9) | (d.month() << 5) | d.day());
}

MediaPackWriter::MediaPackWriter(const QString &path)
    : m_file(path)
{
    m_file.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

MediaPackWriter::~MediaPackWriter()
{
    if (m_file.isOpen())
        m_file.close();
}

bool MediaPackWriter::writeLocal(const QByteArray &name, const QByteArray &bytes, QString *error)
{
    if (!m_file.isOpen()) {
        if (error)
            *error = QStringLiteral("图片包无法写入：%1").arg(m_file.fileName());
        return false;
    }

    quint16 dosTime = 0;
    quint16 dosDate = 0;
    dosDateTime(&dosTime, &dosDate);
    const quint32 crc = crc32(bytes);
    const quint32 offset = quint32(m_file.pos());

    QDataStream out(&m_file);
    out.setByteOrder(QDataStream::LittleEndian);
    out << kLocalSig;
    out << quint16(20);          // version needed
    out << quint16(0);           // flags
    out << quint16(0);           // method = store
    out << dosTime << dosDate;
    out << crc;
    out << quint32(bytes.size()); // compressed size
    out << quint32(bytes.size()); // uncompressed size
    out << quint16(name.size());
    out << quint16(0);           // extra len
    m_file.write(name);
    m_file.write(bytes);

    CentralEntry entry;
    entry.name = name;
    entry.crc = crc;
    entry.size = quint32(bytes.size());
    entry.offset = offset;
    entry.dosTime = dosTime;
    entry.dosDate = dosDate;
    m_entries.append(entry);
    return true;
}

bool MediaPackWriter::addBytes(const QString &entryName, const QByteArray &bytes, QString *error)
{
    if (entryName.isEmpty()) {
        if (error)
            *error = QStringLiteral("条目名为空");
        return false;
    }
    return writeLocal(entryName.toUtf8(), bytes, error);
}

bool MediaPackWriter::addFile(const QString &entryName, const QString &sourcePath, QString *error)
{
    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取图片：%1").arg(sourcePath);
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        if (error)
            *error = QStringLiteral("图片为空：%1").arg(sourcePath);
        return false;
    }
    return writeLocal(entryName.toUtf8(), bytes, error);
}

bool MediaPackWriter::finish(QString *error)
{
    if (m_finished)
        return true;
    if (!m_file.isOpen()) {
        if (error)
            *error = QStringLiteral("图片包未打开");
        return false;
    }

    const quint32 centralOffset = quint32(m_file.pos());
    QDataStream out(&m_file);
    out.setByteOrder(QDataStream::LittleEndian);
    for (const CentralEntry &entry : m_entries) {
        out << kCentralSig;
        out << quint16(20);   // version made by
        out << quint16(20);   // version needed
        out << quint16(0);    // flags
        out << quint16(0);    // method
        out << entry.dosTime << entry.dosDate;
        out << entry.crc;
        out << entry.size;
        out << entry.size;
        out << quint16(entry.name.size());
        out << quint16(0);    // extra
        out << quint16(0);    // comment
        out << quint16(0);    // disk start
        out << quint16(0);    // internal attrs
        out << quint32(0);    // external attrs
        out << entry.offset;
        m_file.write(entry.name);
    }
    const quint32 centralSize = quint32(m_file.pos()) - centralOffset;

    out << kEndSig;
    out << quint16(0) << quint16(0);
    out << quint16(m_entries.size()) << quint16(m_entries.size());
    out << centralSize;
    out << centralOffset;
    out << quint16(0);

    m_file.flush();
    m_file.close();
    m_finished = true;
    return true;
}

// ---------------------------------------------------------------- reader

bool MediaPackReader::open(const QString &path, QString *error)
{
    close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开图片包：%1").arg(path);
        return false;
    }

    const qint64 size = m_file.size();
    if (size < 22) {
        if (error)
            *error = QStringLiteral("图片包太小：%1").arg(path);
        close();
        return false;
    }

    // 找 EOCD（可能带注释，最多扫最后 64KB）
    const qint64 scan = qMin<qint64>(size, 65536 + 22);
    QByteArray tail = m_file.read(scan);
    m_file.seek(size - scan);
    tail = m_file.read(scan);
    int eocd = -1;
    for (int i = tail.size() - 22; i >= 0; --i) {
        if (quint8(tail.at(i)) == 0x50 && quint8(tail.at(i + 1)) == 0x4b
            && quint8(tail.at(i + 2)) == 0x05 && quint8(tail.at(i + 3)) == 0x06) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        if (error)
            *error = QStringLiteral("不是有效的 zip：%1").arg(path);
        close();
        return false;
    }

    auto u16 = [&tail](int pos) { return quint16(quint8(tail.at(pos)) | (quint8(tail.at(pos + 1)) << 8)); };
    auto u32 = [&tail](int pos) {
        return quint32(quint8(tail.at(pos)) | (quint8(tail.at(pos + 1)) << 8)
                       | (quint8(tail.at(pos + 2)) << 16) | (quint8(tail.at(pos + 3)) << 24));
    };

    const quint16 count = u16(eocd + 10);
    const quint32 centralSize = u32(eocd + 12);
    const quint32 centralOffset = u32(eocd + 16);

    if (centralOffset + centralSize > quint64(size)) {
        if (error)
            *error = QStringLiteral("中央目录越界：%1").arg(path);
        close();
        return false;
    }

    m_file.seek(centralOffset);
    const QByteArray central = m_file.read(centralSize);
    int pos = 0;
    while (pos + 46 <= central.size() && m_entries.size() < count) {
        if (quint32(quint8(central.at(pos)) | (quint8(central.at(pos + 1)) << 8)
                    | (quint8(central.at(pos + 2)) << 16) | (quint8(central.at(pos + 3)) << 24)) != kCentralSig)
            break;
        Entry entry;
        entry.method = quint16(quint8(central.at(pos + 10)) | (quint8(central.at(pos + 11)) << 8));
        entry.crc = quint32(quint8(central.at(pos + 16)) | (quint8(central.at(pos + 17)) << 8)
                            | (quint8(central.at(pos + 18)) << 16) | (quint8(central.at(pos + 19)) << 24));
        entry.compressedSize = quint32(quint8(central.at(pos + 20)) | (quint8(central.at(pos + 21)) << 8)
                                       | (quint8(central.at(pos + 22)) << 16) | (quint8(central.at(pos + 23)) << 24));
        entry.size = quint32(quint8(central.at(pos + 24)) | (quint8(central.at(pos + 25)) << 8)
                             | (quint8(central.at(pos + 26)) << 16) | (quint8(central.at(pos + 27)) << 24));
        const quint16 nameLen = quint16(quint8(central.at(pos + 28)) | (quint8(central.at(pos + 29)) << 8));
        const quint16 extraLen = quint16(quint8(central.at(pos + 30)) | (quint8(central.at(pos + 31)) << 8));
        const quint16 commentLen = quint16(quint8(central.at(pos + 32)) | (quint8(central.at(pos + 33)) << 8));
        entry.localOffset = quint32(quint8(central.at(pos + 42)) | (quint8(central.at(pos + 43)) << 8)
                                    | (quint8(central.at(pos + 44)) << 16) | (quint8(central.at(pos + 45)) << 24));
        if (pos + 46 + nameLen > central.size())
            break;
        const QString name = QString::fromUtf8(central.mid(pos + 46, nameLen));
        entry.valid = (entry.method == 0);
        if (entry.valid)
            m_entries.insert(name, entry);
        pos += 46 + nameLen + extraLen + commentLen;
    }

    if (m_entries.isEmpty()) {
        if (error)
            *error = QStringLiteral("图片包里没有可用条目（只支持 store 模式）：%1").arg(path);
        close();
        return false;
    }
    m_opened = true;
    return true;
}

void MediaPackReader::close()
{
    if (m_file.isOpen())
        m_file.close();
    m_entries.clear();
    m_opened = false;
}

qint64 MediaPackReader::entrySize(const QString &name) const
{
    const auto it = m_entries.constFind(name);
    return it == m_entries.constEnd() ? -1 : qint64(it->size);
}

QByteArray MediaPackReader::read(const QString &name)
{
    const auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd() || !m_file.isOpen())
        return {};

    m_file.seek(it->localOffset);
    const QByteArray local = m_file.read(30);
    if (local.size() < 30)
        return {};
    const quint16 nameLen = quint16(quint8(local.at(26)) | (quint8(local.at(27)) << 8));
    const quint16 extraLen = quint16(quint8(local.at(28)) | (quint8(local.at(29)) << 8));
    m_file.seek(it->localOffset + 30 + nameLen + extraLen);
    const QByteArray bytes = m_file.read(it->compressedSize);
    if (quint32(bytes.size()) != it->compressedSize)
        return {};
    if (MediaPackWriter::crc32(bytes) != it->crc)
        return {};
    return bytes;
}
