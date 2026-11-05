#include "MultiplayerSession.h"
#include "PortableSettings.h"

#include <QDataStream>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace {
QStringList kColorPalette()
{
    // 16 色调色板：按顺序默认分配，房间内互斥。
    return QStringList{
        QStringLiteral("#5B8CFF"), QStringLiteral("#FF5C7A"), QStringLiteral("#FFB020"),
        QStringLiteral("#A78BFA"), QStringLiteral("#22D3EE"), QStringLiteral("#4ADE80"),
        QStringLiteral("#FB7185"), QStringLiteral("#FACC15"), QStringLiteral("#F472B6"),
        QStringLiteral("#38BDF8"), QStringLiteral("#34D399"), QStringLiteral("#A3E635"),
        QStringLiteral("#FB923C"), QStringLiteral("#818CF8"), QStringLiteral("#E879F9"),
        QStringLiteral("#F87171"),
    };
}

QJsonObject playerJson(const MultiplayerSession::Player &player)
{
    return QJsonObject{
        {QStringLiteral("id"), player.id},
        {QStringLiteral("name"), player.name},
        {QStringLiteral("host"), player.host},
        {QStringLiteral("connected"), player.connected},
        {QStringLiteral("ready"), player.ready},
        {QStringLiteral("address"), player.address},
        {QStringLiteral("colorIndex"), player.colorIndex},
    };
}

QVector<MultiplayerSession::Player> playersFromJson(const QJsonArray &array)
{
    QVector<MultiplayerSession::Player> out;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        MultiplayerSession::Player player;
        player.id = object.value(QStringLiteral("id")).toInt();
        player.name = object.value(QStringLiteral("name")).toString();
        player.host = object.value(QStringLiteral("host")).toBool();
        player.connected = object.value(QStringLiteral("connected")).toBool(true);
        player.ready = object.value(QStringLiteral("ready")).toBool(false);
        player.address = object.value(QStringLiteral("address")).toString();
        player.colorIndex = object.value(QStringLiteral("colorIndex")).toInt(-1);
        if (player.id >= 0 && !player.name.isEmpty())
            out.append(player);
    }
    return out;
}
} // namespace

QStringList MultiplayerSession::colorPalette()
{
    return kColorPalette();
}

bool MultiplayerSession::setPlayerColor(int playerId, int colorIndex)
{
    if (!m_isHost)
        return false;
    const QStringList palette = kColorPalette();
    if (colorIndex < 0 || colorIndex >= palette.size())
        return false;
    // 别人已经用着的颜色不能抢。
    for (const Player &player : m_players) {
        if (player.id != playerId && player.colorIndex == colorIndex)
            return false;
    }
    for (Player &player : m_players) {
        if (player.id != playerId)
            continue;
        player.colorIndex = colorIndex;
        qInfo().noquote() << "玩家换色:" << player.name << palette.at(colorIndex)
                          << "index" << colorIndex;
        sendLobby();
        emit lobbyChanged();
        return true;
    }
    return false;
}

int MultiplayerSession::nextFreeColorIndex() const
{
    const int total = kColorPalette().size();
    for (int index = 0; index < total; ++index) {
        bool used = false;
        for (const Player &player : m_players) {
            if (player.colorIndex == index) {
                used = true;
                break;
            }
        }
        if (!used)
            return index;
    }
    return -1;
}

MultiplayerSession::MultiplayerSession(QObject *parent)
    : QObject(parent)
{
    m_clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        // 持久化：进程重启后仍能被房主认出来（30 秒窗口内可恢复原位）。
        QSettings &settings = *PortableSettings::appSettings();
        const QString saved = settings.value(QStringLiteral("net/clientId")).toString();
        if (!saved.isEmpty())
            m_clientId = saved;
        else
            settings.setValue(QStringLiteral("net/clientId"), m_clientId);
    }

    m_dropTimer = new QTimer(this);
    m_dropTimer->setInterval(1000);
    connect(m_dropTimer, &QTimer::timeout, this, &MultiplayerSession::tickDropDeadlines);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(2000);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_socket || m_socket->state() != QAbstractSocket::UnconnectedState)
            return;
        if (m_reconnectAttempt >= 3) {
            stopClientReconnect();
            emit reconnectFailed(QStringLiteral("房主已离开，房间已解散"));
            return;
        }
        ++m_reconnectAttempt;
        emit reconnecting(m_reconnectAttempt, 3);
        m_socket->connectToHost(m_joinHost, m_joinPort);
    });
}

void MultiplayerSession::startClientReconnect()
{
    if (m_isHost || m_joinHost.isEmpty() || !m_reconnectTimer)
        return;
    m_reconnectAttempt = 0;
    emit reconnecting(0, 3);
    m_reconnectTimer->start();
}

void MultiplayerSession::stopClientReconnect()
{
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    m_reconnectAttempt = 0;
}

void MultiplayerSession::tickDropDeadlines()
{
    if (!m_isHost)
        return;
    const QDateTime now = QDateTime::currentDateTime();
    QVector<int> expired;
    for (auto it = m_dropDeadlines.cbegin(); it != m_dropDeadlines.cend(); ++it) {
        if (it.value() <= now)
            expired.append(it.key());
    }
    for (int id : expired) {
        m_dropDeadlines.remove(id);
        QString name;
        for (int i = m_players.size() - 1; i >= 0; --i) {
            if (m_players.at(i).id == id) {
                name = m_players.at(i).name;
                m_players.removeAt(i);
            }
        }
        qWarning().noquote() << "重连超时，判弃赛:" << name;
        sendLobby();
        emit lobbyChanged();
        emit peerDropped(id, name);
    }
    if (m_dropDeadlines.isEmpty() && m_dropTimer)
        m_dropTimer->stop();
}

MultiplayerSession::~MultiplayerSession()
{
    leave();
}

QByteArray MultiplayerSession::frame(const QJsonObject &message)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(payload.size());
    frame.append(payload);
    return frame;
}

void MultiplayerSession::sendFrame(QTcpSocket *socket, const QJsonObject &message)
{
    if (socket)
        socket->write(frame(message));
}

QString MultiplayerSession::localIPv4()
{
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol
            && !address.isLoopback()) {
            return address.toString();
        }
    }
    return QStringLiteral("127.0.0.1");
}

QString MultiplayerSession::parseHost(QString address, quint16 *port)
{
    address = address.trimmed();
    if (address.isEmpty())
        return {};
    if (!address.contains(QStringLiteral("://")))
        address.prepend(QStringLiteral("http://"));
    const QUrl url(address);
    if (!url.isValid())
        return {};
    const int parsedPort = url.port(0);
    if (parsedPort <= 0 || parsedPort > 65535)
        return {};
    *port = static_cast<quint16>(parsedPort);
    return url.host();
}

bool MultiplayerSession::startHost(
    const QString &name, const QString &assetHash, const QString &bankLabel,
    QString *error, quint16 preferredPort)
{
    leave();
    m_isHost = true;
    m_localName = name.trimmed();
    m_assetHash = assetHash;
    m_bankLabel = bankLabel;
    m_localId = 0;
    m_nextPlayerId = 1;
    m_players = {Player{0, m_localName, true, true, false, m_clientId, localIPv4()}};
    m_players.first().colorIndex = nextFreeColorIndex();

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        while (m_server && m_server->hasPendingConnections())
            addClient(m_server->nextPendingConnection());
    });
    if (!m_server->listen(QHostAddress::AnyIPv4, preferredPort)) {
        if (error) {
            *error = preferredPort == 0
                ? m_server->errorString()
                : QStringLiteral("端口 %1 无法监听（可能被占用），请换一个端口。").arg(preferredPort);
        }
        delete m_server;
        m_server = nullptr;
        m_isHost = false;
        return false;
    }
    m_roomAddress = QStringLiteral("%1:%2")
                        .arg(localIPv4())
                        .arg(m_server->serverPort());
    m_connected = true;
    emit roomReady();
    emit lobbyChanged();
    return true;
}

bool MultiplayerSession::join(
    const QString &name, const QString &address,
    const QString &assetHash, const QString &bankLabel, QString *error)
{
    leave();
    quint16 port = 0;
    const QString host = parseHost(address, &port);
    if (host.isEmpty() || port == 0) {
        if (error)
            *error = QStringLiteral("地址格式应为 IP:端口 或 http://IP:端口");
        return false;
    }
    m_isHost = false;
    m_localName = name.trimmed();
    m_assetHash = assetHash;
    m_bankLabel = bankLabel;
    m_localId = -1;
    // 客户端也要知道自己连的是哪个房间：掉线后主菜单的「重新连接」靠它。
    m_roomAddress = QStringLiteral("%1:%2").arg(host).arg(port);
    m_socket = new QTcpSocket(this);
    m_joinHost = host;
    m_joinPort = port;
    m_handshakeDone = false;
    m_rejectReason.clear();
    connect(m_socket, &QTcpSocket::connected, this, [this]() {
        QJsonObject hello{
            {QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("name"), m_localName},
            {QStringLiteral("assetHash"), m_assetHash},
            {QStringLiteral("bankLabel"), m_bankLabel},
            {QStringLiteral("protocol"), 1},
            {QStringLiteral("clientId"), m_clientId},
        };
        sendFrame(m_socket, hello);
    });
    connect(m_socket, &QTcpSocket::readyRead, this, &MultiplayerSession::onClientReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        m_connected = false;
        if (m_suppressDisconnect || m_isHost || m_joinHost.isEmpty())
            return;
        // 握手没完成就断了：多半是房主拒绝（房间已开局 / 版本不符 / 已满）。
        if (!m_handshakeDone) {
            stopClientReconnect();
            emit errorOccurred(m_rejectReason.isEmpty()
                                   ? QStringLiteral("无法加入：房主可能已开始游戏、房间已关闭，或网络不通。")
                                   : m_rejectReason);
            return;
        }
        // 房主掉线 = 房间没了：不再自动重连（房主不在，重连也没意义），
        // 直接交给上层回主菜单；玩家想再试的话在主菜单手动重连。
        stopClientReconnect();
        emit disconnected(QStringLiteral("房主已离开，房间已解散"));
    });
    connect(m_socket, &QAbstractSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError error) {
        if (m_connected || !m_socket)
            return;
        switch (error) {
        case QAbstractSocket::ConnectionRefusedError:
            emit errorOccurred(QStringLiteral(
                "连接被拒绝：房主没有开房、端口未开放，或被防火墙拦截。"));
            break;
        case QAbstractSocket::HostNotFoundError:
            emit errorOccurred(QStringLiteral("找不到这个地址，请检查域名或 IP 是否输入正确。"));
            break;
        case QAbstractSocket::SocketTimeoutError:
            emit errorOccurred(QStringLiteral("连接超时，请检查网络或房主是否在线。"));
            break;
        default:
            emit errorOccurred(m_socket->errorString());
            break;
        }
    });
    m_socket->connectToHost(host, port);
    return true;
}

void MultiplayerSession::leave()
{
    m_suppressDisconnect = true;
    stopClientReconnect();
    if (m_dropTimer)
        m_dropTimer->stop();
    m_dropDeadlines.clear();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    for (Client *client : m_clients) {
        if (client->socket) {
            client->socket->disconnect(this);
            client->socket->abort();
            client->socket->deleteLater();
        }
        delete client;
    }
    m_clients.clear();
    m_players.clear();
    m_clientBuffer.clear();
    m_connected = false;
    m_isHost = false;
    m_roomStarted = false;
    m_localId = 0;
    m_roomAddress.clear();
    m_joinHost.clear();
    m_joinPort = 0;
    m_suppressDisconnect = false;
}

void MultiplayerSession::broadcast(const QJsonObject &message)
{
    const QByteArray bytes = frame(message);
    for (Client *client : m_clients) {
        if (client->playerId >= 0 && client->socket)
            client->socket->write(bytes);
    }
}

void MultiplayerSession::setRoomStarted(bool started)
{
    m_roomStarted = started;
}

void MultiplayerSession::setPlayerReady(int playerId, bool ready)
{
    if (!m_isHost || m_roomStarted)
        return;
    for (Player &player : m_players) {
        if (player.id == playerId && !player.host) {
            player.ready = ready;
            sendLobby();
            emit lobbyChanged();
            return;
        }
    }
}

void MultiplayerSession::resetReadyStates()
{
    if (!m_isHost)
        return;
    for (Player &player : m_players)
        player.ready = false;
    sendLobby();
    emit lobbyChanged();
}

void MultiplayerSession::send(const QJsonObject &message)
{
    if (!m_isHost && m_socket)
        sendFrame(m_socket, message);
}

void MultiplayerSession::sendTo(int playerId, const QJsonObject &message)
{
    const QByteArray bytes = frame(message);
    for (Client *client : m_clients) {
        if (client->playerId == playerId && client->socket)
            client->socket->write(bytes);
    }
}

void MultiplayerSession::addClient(QTcpSocket *socket)
{
    if (!socket)
        return;
    if (m_roomStarted) {
        // 比赛进行中也允许接入：只有 clientId 能对上的老玩家才算重连，
        // 陌生连接在 hello 阶段会被拒绝（见 handleClientMessage）。
    }
    if (m_players.size() >= 4) {
        sendFrame(socket, QJsonObject{
            {QStringLiteral("type"), QStringLiteral("rejected")},
            {QStringLiteral("reason"), QStringLiteral("房间已满（最多 4 人）")},
        });
        socket->disconnectFromHost();
        socket->deleteLater();
        return;
    }
    auto *client = new Client;
    client->socket = socket;
    client->playerId = -1;
    m_clients.append(client);
    connect(socket, &QTcpSocket::readyRead, this, [this, client]() {
        onHostReadyRead(client);
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, client]() {
        removeClient(client, QStringLiteral("玩家已离开"));
    });
}

void MultiplayerSession::onHostReadyRead(Client *client)
{
    if (!client || !client->socket)
        return;
    client->buffer.append(client->socket->readAll());
    while (client->buffer.size() >= 4) {
        QDataStream stream(&client->buffer, QIODevice::ReadOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        quint32 size = 0;
        stream >> size;
        if (size > 2 * 1024 * 1024) {
            removeClient(client, QStringLiteral("消息过大"));
            return;
        }
        if (client->buffer.size() < int(4 + size))
            break;
        const QByteArray payload = client->buffer.mid(4, int(size));
        client->buffer.remove(0, int(4 + size));
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (document.isObject())
            handleClientMessage(client, document.object());
        if (!m_clients.contains(client))
            return;
    }
}

void MultiplayerSession::handleClientMessage(
    Client *client, const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    // 主动离开：房主立即移除，不走 30 秒重连等待。
    if (type == QStringLiteral("leave") && client->playerId >= 0) {
        const int leavingId = client->playerId;
        QString leavingName;
        for (int i = m_players.size() - 1; i >= 0; --i) {
            if (m_players.at(i).id == leavingId) {
                leavingName = m_players.at(i).name;
                m_players.removeAt(i);
            }
        }
        m_dropDeadlines.remove(leavingId);
        client->playerId = -1;
        sendLobby();
        emit lobbyChanged();
        emit peerDropped(leavingId, leavingName);
        return;
    }
    if (client->playerId < 0) {
        if (type != QStringLiteral("hello")) {
            sendFrame(client->socket, QJsonObject{
                {QStringLiteral("type"), QStringLiteral("rejected")},
                {QStringLiteral("reason"), QStringLiteral("需要先完成握手")},
            });
            client->socket->disconnectFromHost();
            return;
        }
        const QString name = message.value(QStringLiteral("name")).toString().trimmed();
        const QString hash = message.value(QStringLiteral("assetHash")).toString();
        const int protocol = message.value(QStringLiteral("protocol")).toInt();
        if (protocol != 1) {
            sendFrame(client->socket, QJsonObject{
                {QStringLiteral("type"), QStringLiteral("rejected")},
                {QStringLiteral("reason"), QStringLiteral("协议版本不一致")},
            });
            client->socket->disconnectFromHost();
            return;
        }
        if (hash != m_assetHash) {
            const QString clientBank = message.value(QStringLiteral("bankLabel")).toString();
            QString reason = QStringLiteral("题库版本不同（房主与客机的 questions.nteq / media.ntep 不一致）");
            if (!m_bankLabel.isEmpty() && !clientBank.isEmpty()) {
                reason = QStringLiteral("题库版本不同（房主 %1，你 %2）")
                             .arg(m_bankLabel, clientBank);
            }
            sendFrame(client->socket, QJsonObject{
                {QStringLiteral("type"), QStringLiteral("rejected")},
                {QStringLiteral("reason"), reason},
            });
            qInfo().noquote() << "拒绝加入：题库版本不同，房主" << m_bankLabel
                              << "客机" << clientBank;
            client->socket->disconnectFromHost();
            return;
        }
        if (name.isEmpty()) {
            sendFrame(client->socket, QJsonObject{
                {QStringLiteral("type"), QStringLiteral("rejected")},
                {QStringLiteral("reason"), QStringLiteral("玩家名字不能为空")},
            });
            client->socket->disconnectFromHost();
            return;
        }
        const QString clientId = message.value(QStringLiteral("clientId")).toString();
        if (!clientId.isEmpty()) {
            for (Player &player : m_players) {
                // 只有"当前确实是掉线状态"的老玩家才算重连；
                // 否则同 clientId 的第二个实例会顶掉正在游戏的那个人。
                if (!player.host && player.clientId == clientId && !player.connected) {
                    player.connected = true;
                    player.ready = false;
                    player.address = client->socket ? client->socket->peerAddress().toString()
                                                    : player.address;
                    client->playerId = player.id;
                    m_dropDeadlines.remove(player.id);
                    if (m_dropDeadlines.isEmpty() && m_dropTimer)
                        m_dropTimer->stop();

                    QJsonArray players;
                    for (const Player &item : m_players)
                        players.append(playerJson(item));
                    sendFrame(client->socket, QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("welcome")},
                        {QStringLiteral("playerId"), client->playerId},
                        {QStringLiteral("players"), players},
                        {QStringLiteral("resumed"), true},
                        {QStringLiteral("roomStarted"), m_roomStarted},
                    });
                    sendLobby();
                    emit lobbyChanged();
                    qInfo().noquote() << "玩家重连:" << player.name << "id" << player.id;
                    emit peerReconnected(player.id, player.name);
                    return;
                }
            }
        }

        // 比赛进行中：只有 clientId 能对上的老玩家才算重连，陌生连接拒绝。
        if (m_roomStarted) {
            sendFrame(client->socket, QJsonObject{
                {QStringLiteral("type"), QStringLiteral("rejected")},
                {QStringLiteral("reason"), QStringLiteral("房主已开始游戏，无法加入")},
            });
            client->socket->disconnectFromHost();
            return;
        }

        // 房间里不允许重名。
        for (const Player &existing : m_players) {
            if (existing.name.compare(name, Qt::CaseInsensitive) == 0) {
                sendFrame(client->socket, QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("rejected")},
                    {QStringLiteral("reason"), QStringLiteral("房间里已有同名玩家，请换个名字")},
                });
                client->socket->disconnectFromHost();
                return;
            }
        }

        client->playerId = m_nextPlayerId++;
        Player player;
        player.id = client->playerId;
        player.name = name;
        player.host = false;
        player.connected = true;
        // 同一台电脑多开时，两个实例可能共用同一份 clientId（QSettings 共享）；
        // 这里给后来者换一个新的，避免重连时互相顶号。
        QString effectiveClientId = clientId;
        for (const Player &existing : m_players) {
            if (!existing.clientId.isEmpty() && existing.clientId == effectiveClientId) {
                effectiveClientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                break;
            }
        }
        player.clientId = effectiveClientId;
        if (client->socket)
            player.address = client->socket->peerAddress().toString();
        player.colorIndex = nextFreeColorIndex();   // 进房默认按调色板顺序分配
        m_players.append(player);

        QJsonArray players;
        for (const Player &item : m_players)
            players.append(playerJson(item));
        sendFrame(client->socket, QJsonObject{
            {QStringLiteral("type"), QStringLiteral("welcome")},
            {QStringLiteral("playerId"), client->playerId},
            {QStringLiteral("players"), players},
            {QStringLiteral("clientId"), effectiveClientId},
            {QStringLiteral("roomStarted"), m_roomStarted},
        });
        sendLobby();
        emit lobbyChanged();
        return;
    }
    // 客户端请求换色：由房主校验互斥后统一广播。
    if (type == QStringLiteral("color")) {
        setPlayerColor(client->playerId, message.value(QStringLiteral("index")).toInt(-1));
        return;
    }
    // 客户端主动要求重发一次名单（进房间界面时用它兜底同步颜色/名字）。
    if (type == QStringLiteral("lobby_request")) {
        sendLobby();
        return;
    }
    emit messageReceived(client->playerId, message);
}

void MultiplayerSession::onClientReadyRead()
{
    if (!m_socket)
        return;
    m_clientBuffer.append(m_socket->readAll());
    while (m_clientBuffer.size() >= 4) {
        QDataStream stream(&m_clientBuffer, QIODevice::ReadOnly);
        stream.setByteOrder(QDataStream::BigEndian);
        quint32 size = 0;
        stream >> size;
        if (m_clientBuffer.size() < int(4 + size))
            break;
        const QByteArray payload = m_clientBuffer.mid(4, int(size));
        m_clientBuffer.remove(0, int(4 + size));
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (document.isObject())
            handleServerMessage(document.object());
    }
}

void MultiplayerSession::handleServerMessage(const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("welcome")) {
        m_localId = message.value(QStringLiteral("playerId")).toInt();
        m_players = playersFromJson(message.value(QStringLiteral("players")).toArray());
        // 房主可能给我换了一个 clientId（同一台电脑多开时可能重复），换了就用新的。
        const QString assigned = message.value(QStringLiteral("clientId")).toString();
        if (!assigned.isEmpty() && assigned != m_clientId)
            m_clientId = assigned;
        m_connected = true;
        m_handshakeDone = true;
        if (message.value(QStringLiteral("resumed")).toBool()) {
            stopClientReconnect();
            qInfo().noquote() << "重连成功，playerId" << m_localId;
            emit reconnected();
        } else if (m_roomStarted) {
            // 房主不认识这条连接（对方重建了房间）：退出对局回到大厅。
            stopClientReconnect();
            m_roomStarted = false;
            qWarning().noquote() << "房主未识别本客户端，退回大厅";
            emit sessionReset();
        }
        emit roomReady();
        emit lobbyChanged();
        return;
    }
    if (type == QStringLiteral("lobby")) {
        m_players = playersFromJson(message.value(QStringLiteral("players")).toArray());
        emit lobbyChanged();
        return;
    }
    if (type == QStringLiteral("rejected")) {
        const QString reason = message.value(QStringLiteral("reason")).toString();
        m_rejectReason = reason;
        emit errorOccurred(reason);
        m_socket->disconnectFromHost();
        return;
    }
    emit messageReceived(0, message);
}

void MultiplayerSession::sendLobby(QTcpSocket *socket)
{
    QJsonArray players;
    for (const Player &player : m_players)
        players.append(playerJson(player));
    const QJsonObject message{
        {QStringLiteral("type"), QStringLiteral("lobby")},
        {QStringLiteral("players"), players},
    };
    if (socket)
        sendFrame(socket, message);
    else
        broadcast(message);
}

void MultiplayerSession::removeClient(Client *client, const QString &reason)
{
    if (!client)
        return;
    const int id = client->playerId;
    if (client->socket) {
        client->socket->disconnect(this);
        client->socket->deleteLater();
    }
    m_clients.removeOne(client);
    delete client;
    if (id < 0) {
        Q_UNUSED(reason)
        return;
    }

    int index = -1;
    for (int i = 0; i < m_players.size(); ++i) {
        if (m_players.at(i).id == id) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        Q_UNUSED(reason)
        return;
    }

    const Player player = m_players.at(index);
    // 比赛进行中：先保留 30 秒等重连，超时由 tickDropDeadlines 判弃赛。
    if (m_roomStarted && !player.clientId.isEmpty() && !player.host) {
        m_players[index].connected = false;
        m_players[index].ready = false;
        m_dropDeadlines.insert(id, QDateTime::currentDateTime().addSecs(30));
        if (m_dropTimer)
            m_dropTimer->start();
        sendLobby();
        emit lobbyChanged();
        qWarning().noquote() << "玩家掉线，等待重连(30s):" << player.name;
        emit peerDisconnected(id, player.name);
        Q_UNUSED(reason)
        return;
    }

    m_players.removeAt(index);
    sendLobby();
    emit lobbyChanged();
    Q_UNUSED(reason)
}
