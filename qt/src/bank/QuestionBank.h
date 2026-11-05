#pragma once

#include "MediaPack.h"

#include <QHash>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

// 一条题目（题库里的最小单位）
struct BankQuestion {
    QString id;
    QString locationId;
    QString type = QStringLiteral("photo");
    QString district;
    QString area;
    QString title;
    double x = 0.0;
    double y = 0.0;
    double gameX = 0.0;
    double gameY = 0.0;
    QString mapVersion;
    QString coordFrame;
    QString status = QStringLiteral("approved");
    QString mediaId;
    QString contributor;
    bool contributorAnon = false;
    QString photographer;
    bool photographerAnon = false;
    QString capturedAt;
    QString createdAt;
    QString updatedAt;
};

// 只读题库：游戏侧用（questions.nteq + media.ntep）
class QuestionBank
{
public:
    ~QuestionBank();

    bool open(const QString &bankPath, const QString &mediaPackPath, QString *error = nullptr);
    void close();
    bool isOpen() const { return m_open; }

    QString bankId() const { return m_bankId; }
    QString bankVersion() const { return m_bankVersion; }
    QString mediaPackHash() const { return m_mediaPackHash; }

    QVector<BankQuestion> questions() const;              // 全部
    QVector<BankQuestion> approvedQuestions() const;      // status=approved

    // 取图片原始字节：先按 media_id 查扩展名，再从 .ntep 里读。
    QByteArray mediaBytes(const QString &mediaId);
    QString mediaEntryName(const QString &mediaId) const;

private:
    QSqlDatabase m_db;
    QString m_connectionName;
    MediaPackReader m_pack;
    QHash<QString, QString> m_mediaExt;   // id -> ext
    bool m_open = false;
    QString m_bankId;
    QString m_bankVersion;
    QString m_mediaPackHash;
};


