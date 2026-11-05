#include "QuestionBank.h"

#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

namespace {

QString newConnectionName(const QString &prefix)
{
    static int counter = 0;
    return QStringLiteral("%1_%2").arg(prefix).arg(++counter);
}

} // namespace

// ------------------------------------------------------------------ reader

QuestionBank::~QuestionBank()
{
    close();
}

bool QuestionBank::open(const QString &bankPath, const QString &mediaPackPath, QString *error)
{
    close();
    if (!QFileInfo::exists(bankPath)) {
        if (error)
            *error = QStringLiteral("题库不存在：%1").arg(bankPath);
        return false;
    }

    m_connectionName = newConnectionName(QStringLiteral("bank_ro"));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(bankPath);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!m_db.open()) {
        if (error)
            *error = QStringLiteral("打不开题库：%1").arg(m_db.lastError().text());
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
        return false;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT key, value FROM meta"))) {
        if (error)
            *error = QStringLiteral("题库缺少 meta 表：%1").arg(query.lastError().text());
        close();
        return false;
    }
    QHash<QString, QString> meta;
    while (query.next())
        meta.insert(query.value(0).toString(), query.value(1).toString());

    const QString format = meta.value(QStringLiteral("formatVersion"));
    if (format != QStringLiteral("2")) {
        if (error)
            *error = QStringLiteral("题库格式版本不支持：%1（需要 formatVersion=2）").arg(format);
        close();
        return false;
    }
    m_bankId = meta.value(QStringLiteral("bankId"), QStringLiteral("official"));
    m_bankVersion = meta.value(QStringLiteral("bankVersion"));
    m_mediaPackHash = meta.value(QStringLiteral("mediaPackHash"));

    QSqlQuery mediaQuery(m_db);
    if (mediaQuery.exec(QStringLiteral("SELECT id, ext FROM media"))) {
        while (mediaQuery.next())
            m_mediaExt.insert(mediaQuery.value(0).toString(), mediaQuery.value(1).toString());
    }

    if (!mediaPackPath.isEmpty()) {
        QString packError;
        if (!m_pack.open(mediaPackPath, &packError)) {
            if (error)
                *error = packError;
            close();
            return false;
        }
    }

    m_open = true;
    return true;
}

void QuestionBank::close()
{
    m_pack.close();
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }
    m_mediaExt.clear();
    m_open = false;
}

QVector<BankQuestion> QuestionBank::questions() const
{
    QVector<BankQuestion> out;
    if (!m_open)
        return out;

    // 兼容早期题库：匿名标记/拍摄者字段可能还不存在，缺失时按默认值读取。
    QStringList columns;
    QSqlQuery info(m_db);
    if (info.exec(QStringLiteral("PRAGMA table_info(questions)"))) {
        while (info.next())
            columns << info.value(1).toString();
    }
    const QStringList fields{
        QStringLiteral("id"),
        QStringLiteral("location_id"),
        QStringLiteral("type"),
        QStringLiteral("district"),
        QStringLiteral("area"),
        QStringLiteral("title"),
        QStringLiteral("x"),
        QStringLiteral("y"),
        QStringLiteral("game_x"),
        QStringLiteral("game_y"),
        QStringLiteral("map_version"),
        QStringLiteral("coord_frame"),
        QStringLiteral("status"),
        QStringLiteral("media_id"),
        QStringLiteral("contributor"),
        columns.contains(QStringLiteral("contributor_anon")) ? QStringLiteral("contributor_anon")
                                                            : QStringLiteral("0"),
        columns.contains(QStringLiteral("photographer")) ? QStringLiteral("photographer")
                                                        : QStringLiteral("NULL"),
        columns.contains(QStringLiteral("photographer_anon")) ? QStringLiteral("photographer_anon")
                                                              : QStringLiteral("0"),
        columns.contains(QStringLiteral("captured_at")) ? QStringLiteral("captured_at")
                                                       : QStringLiteral("NULL"),
        QStringLiteral("created_at"),
        QStringLiteral("updated_at"),
    };
    QSqlQuery query(m_db);
    query.exec(QStringLiteral("SELECT %1 FROM questions ORDER BY id")
                   .arg(fields.join(QStringLiteral(", "))));
    while (query.next()) {
        BankQuestion q;
        int i = 0;
        q.id = query.value(i++).toString();
        q.locationId = query.value(i++).toString();
        q.type = query.value(i++).toString();
        q.district = query.value(i++).toString();
        q.area = query.value(i++).toString();
        q.title = query.value(i++).toString();
        q.x = query.value(i++).toDouble();
        q.y = query.value(i++).toDouble();
        q.gameX = query.value(i++).toDouble();
        q.gameY = query.value(i++).toDouble();
        q.mapVersion = query.value(i++).toString();
        q.coordFrame = query.value(i++).toString();
        q.status = query.value(i++).toString();
        q.mediaId = query.value(i++).toString();
        q.contributor = query.value(i++).toString();
        q.contributorAnon = query.value(i++).toInt() != 0;
        q.photographer = query.value(i++).toString();
        q.photographerAnon = query.value(i++).toInt() != 0;
        q.capturedAt = query.value(i++).toString();
        q.createdAt = query.value(i++).toString();
        q.updatedAt = query.value(i++).toString();
        out.append(q);
    }
    return out;
}

QVector<BankQuestion> QuestionBank::approvedQuestions() const
{
    QVector<BankQuestion> out;
    for (const BankQuestion &q : questions()) {
        if (q.status == QStringLiteral("approved"))
            out.append(q);
    }
    return out;
}

QString QuestionBank::mediaEntryName(const QString &mediaId) const
{
    const auto it = m_mediaExt.constFind(mediaId);
    if (it == m_mediaExt.constEnd())
        return {};
    return mediaId + QLatin1Char('.') + it.value();
}

QByteArray QuestionBank::mediaBytes(const QString &mediaId)
{
    const QString entry = mediaEntryName(mediaId);
    if (entry.isEmpty())
        return {};
    return m_pack.read(entry);
}
