#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVector>

class QTimer;

// LAN-only room transport for the 2-4 player mode.
// The host is authoritative; this class only handles framing, handshake,
// player membership and JSON message forwarding.
//
// 重连（Step 2）：
//   - 每个客户端进程生成一个 clientId（UUID），hello 时带上；
//   - 房主在比赛进行中把断线玩家保留 30 秒（connected=false），期间同 clientId
//     回来即可恢复原 playerId（HP/得分都在 GameWindow 的 m_mpState 里，天然保留）；
//   - 30 秒未回 → 真正移除，并发出 peerDropped，由上层判弃赛。
class MultiplayerSession : public QObject
{
    Q_OBJECT

public:
    struct Player {
        int id = 0;
        QString name;
        bool host = false;
        bool connected = true;
        bool ready = false;
        QString clientId;
        // 该玩家在局域网里的 IP（房主从连接上读到的）。房主掉线后别的客户端靠它去找接任者。
        QString address;
        // 房间内自己选的主题色：调色板下标（-1 = 未分配）。用下标而不是色值，
        // 保证所有客户端拿到的调色板顺序、颜色完全一致。
        int colorIndex = -1;
    };

    // 可选的玩家颜色（顺序即进房时默认分配的顺序）。
    static QStringList colorPalette();

    explicit MultiplayerSession(QObject *parent = nullptr);
    ~MultiplayerSession() override;

    bool isHost() const { return m_isHost; }
    bool isConnected() const { return m_connected; }
    int localPlayerId() const { return m_localId; }
    QString localName() const { return m_localName; }
    QString roomAddress() const { return m_roomAddress; }
    QVector<Player> players() const { return m_players; }
    QString clientId() const { return m_clientId; }

    // bankLabel 只用于握手失败时给玩家看清「房主/自己」的题库版本，可为空。
    bool startHost(const QString &name, const QString &assetHash, const QString &bankLabel,
                   QString *error, quint16 preferredPort = 0);
    bool join(const QString &name, const QString &address,
              const QString &assetHash, const QString &bankLabel, QString *error);
    void leave();

    void setRoomStarted(bool started);
    void setPlayerReady(int playerId, bool ready);
    // 房主侧：给某个玩家设置颜色下标；越界或被占用则返回 false。客户端发 "color" 请求。
    bool setPlayerColor(int playerId, int colorIndex);
    // 房主侧：调色板里第一个没被占用的下标（进房默认分配）。
    int nextFreeColorIndex() const;
    void resetReadyStates();
    void broadcast(const QJsonObject &message);
    void send(const QJsonObject &message);
    void sendTo(int playerId, const QJsonObject &message);

signals:
    void lobbyChanged();
    void roomReady();
    void messageReceived(int playerId, const QJsonObject &message);
    void errorOccurred(const QString &message);
    void disconnected(const QString &reason);

    // 重连相关
    void peerDisconnected(int playerId, const QString &name);   // 房主侧：有人掉了，开始 30s 计时
    void peerReconnected(int playerId, const QString &name);    // 房主侧：同 clientId 回来了
    void peerDropped(int playerId, const QString &name);        // 房主侧：30s 到，判弃赛
    void reconnecting(int attempt, int maxAttempts);            // 客户端侧：正在重试
    void reconnected();                                          // 客户端侧：重连成功
    void reconnectFailed(const QString &reason);                 // 客户端侧：放弃（房主已解散）
    void sessionReset();                                         // 房主不认识我们了（房间被重建）

private:
    struct Client {
        QTcpSocket *socket = nullptr;
        QByteArray buffer;
        int playerId = -1;
    };

    static QByteArray frame(const QJsonObject &message);
    static void sendFrame(QTcpSocket *socket, const QJsonObject &message);
    static QString parseHost(QString address, quint16 *port);
    static QString localIPv4();

    void onHostReadyRead(Client *client);
    void onClientReadyRead();
    void handleClientMessage(Client *client, const QJsonObject &message);
    void handleServerMessage(const QJsonObject &message);
    void sendLobby(QTcpSocket *socket = nullptr);
    void addClient(QTcpSocket *socket);
    void removeClient(Client *client, const QString &reason);

    void startClientReconnect();
    void stopClientReconnect();
    void tickDropDeadlines();

    QTcpServer *m_server = nullptr;
    QTcpSocket *m_socket = nullptr;
    QVector<Client *> m_clients;
    QVector<Player> m_players;
    QString m_localName;
    QString m_assetHash;
    QString m_bankLabel;
    QString m_roomAddress;
    bool m_isHost = false;
    bool m_connected = false;
    bool m_roomStarted = false;
    int m_localId = 0;
    int m_nextPlayerId = 1;
    QByteArray m_clientBuffer;

    QString m_clientId;
    // 房主侧：clientId -> 掉线截止时间
    QHash<int, QDateTime> m_dropDeadlines;
    QTimer *m_dropTimer = nullptr;

    // 客户端侧：重连参数
    QString m_joinHost;
    quint16 m_joinPort = 0;
    bool m_handshakeDone = false;
    QString m_rejectReason;
    int m_reconnectAttempt = 0;
    QTimer *m_reconnectTimer = nullptr;
    bool m_suppressDisconnect = false;
};
