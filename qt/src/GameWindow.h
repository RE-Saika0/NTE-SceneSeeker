#pragma once

#include <QPointF>
#include <QPixmap>
#include <QRect>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>
#include <QWidget>

class MapOverlay;
class BackgroundCarousel;
class AlarmOverlayWidget;
class QuestionBank;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class QKeyEvent;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QComboBox;
class QTextEdit;
class QJsonObject;
class MultiplayerSession;
class ScreenshotView;
class BattleHudWidget;
class BgmPlayer;
class QSlider;
class QPushButton;

class GameWindow : public QWidget
{
    Q_OBJECT

public:
    // 多人对战初始血量。
    static constexpr int MP_START_HP = 5000;

    // 顶部血条用的条目：2 人局左右对位，3-4 人局一行平分。
    struct HudFighter {
        QString name;
        int hp = MP_START_HP;
        int startHp = MP_START_HP;
        int endHp = MP_START_HP;
        bool local = false;
        bool alive = true;
        // 玩家在房间里选的主题色（空则用内置座位色）。
        QString color;
    };

    explicit GameWindow(QWidget *parent = nullptr);
    ~GameWindow() override;

    // 供右下角地图按钮调用；公开也方便后续做引导或自动化测试。
    void openMapForPick();
    void showStartMenu();
    void startGameForTesting();
    // 开发自检：自动落点并提交，用来看结算回放（单人）。
    void submitForTesting();
    // 开发自检：直接播一次假的多人回合结算（标点靠上，验证不被分数框遮挡）。
    void showFakeMpRoundForTesting();
    // 开发自检：直接显示一次「比赛结束」面板。
    void showFakeMpMatchEndForTesting();
    // 开发自检：直接显示主菜单的「重新连接」卡片。
    void showReconnectPromptForTesting();
    // 开发自检：造 4 名玩家，只显示顶部血条（验证 3-4 人一行平分）。
    void showHudForTesting();
    // 开发自检：直接开一个本地房间并停在房间界面（验证颜色选择等 UI）。
    void showFakeLobbyForTesting();
    // 开发自检：装作房主遇到掉线，显示等待遮罩 + 「结束等待」按钮。
    void showFakePauseForTesting();
    // 按设置里的「全屏启动 + 窗口位置记忆」显示窗口。
    void showWithSavedState();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Question {
        enum class ViewType { Photo, Panorama };

        QString id;
        QString locationId;
        QString imageRelative;
        QString previewRelative;
        QString panoramaRelative;
        // 题库包里这张图的 id（为空表示走文件路径）
        QString mediaId;
        ViewType viewType = ViewType::Photo;
        QString mapVersion;
        QString district;
        QString area;
        QString title;
        QString contributor;
        bool contributorAnon = false;
        QString photographer;
        bool photographerAnon = false;
        double gameX = 0.0;
        double gameY = 0.0;
        QPointF normalized{0.5, 0.5};
        bool isPlaceholder = false;
        int placeholderIndex = 0;
    };

    struct RoundResult {
        int round = 0;
        QPointF guess;
        QPointF truth;
        double distancePx = 0.0;
        int score = 0;
        QString label;
    };

    struct MpPlayerState {
        int id = 0;
        QString name;
        int hp = MP_START_HP;
        bool submitted = false;
        bool hasMarker = false;
        QPointF guess{0.5, 0.5};
        double distanceM = 0.0;
        int score = 0;
        int damage = 0;
        bool active = true;
    };

    enum class GameMode { PhotoOnly, PanoramaOnly, Mixed };

    static constexpr int DEFAULT_ROUNDS = 9;
    static constexpr int DEFAULT_SECONDS_PER_ROUND = 60;
    // 剩余时间进入这个阈值后，屏幕边缘开始红色警报滤镜。
    static constexpr int ALARM_SECONDS = 15;
    static constexpr int PLACEHOLDER_ROUNDS = 5;
    static constexpr int MIN_ROUNDS = 5;
    static constexpr int MAX_ROUNDS = 12;
    static constexpr int MIN_SECONDS_PER_ROUND = 15;
    static constexpr int MAX_SECONDS_PER_ROUND = 300;

    void loadQuestionBank();
    void buildPlaceholderBank();
    void loadSettings();
    void saveSettings();
    void showSettingsMenu();
    void startNewGame();
    void startNewGameWithMode(GameMode mode);
    void showSinglePlayerMenu();
    void showMultiplayerMenu();
    void showMpCreatePage();
    void showMpJoinPage();
    void showMpLobbyPage();
    void createMpRoom();
    void joinMpRoom();
    void leaveMpRoom();
    void applyMpLobbySettings();
    void toggleMpReady();
    void sendMpChat();
    void appendChatSystem(const QString &text);
    void showRoundCountdown();
    void showCountdownNumber(int number);
    void pauseMpTimer(qint64 remainingMs);
    void resumeMpTimer(qint64 remainingMs);
    void showPauseBanner(const QString &text);
    void hidePauseBanner();
    void beginPauseCountdown(const QString &prefix, int seconds);
    // 房主点「结束等待」或 30 秒到点：判掉线玩家弃赛并继续/结束对局。
    void finishMpPauseWait();
    void applyMpSnapshot(const QJsonObject &message);
    void startMpMatch();
    void startMpRound(int index);
    void handleMpMessage(int playerId, const QJsonObject &message);
    void handleMpGuess(int playerId, const QPointF &guess);
    void handleMpPin(int playerId, const QPointF &guess);
    void resolveMpRound();
    void finishMpMatch();
    // 房间只剩一人（其他人掉线/离开）：立刻结束对局并退回房间大厅。
    void abortMpMatchToLobby(const QString &reason);
    int mpAliveCount() const;
    void showMpRoundResult(const QJsonObject &result);
    void updateMpLobby();
    void updateMpHud();
    // 顶部血条里的座位顺序（自己第一，其余按血量降序）。静态刷新与结算动画必须共用。
    QVector<HudFighter> mpHudFighters() const;
    // 房间里的颜色选择（16 色互斥）。
    void chooseMpColor(int index);
    void refreshMpColorSwatches();
    // 主菜单上的「重新连接」卡片（房主不弹）。
    void buildReconnectCard();
    void showReconnectPrompt(const QString &reason);
    void hideReconnectPrompt();
    void reconnectToLastRoom();
    // 记下当前房间（地址 / 自己名字 / 房主名字），用于主菜单的重连提示。
    void rememberRoomForReconnect();
    // 最后 15 秒的红色边缘警报（时深时浅）。
    void updateAlarmOverlay();
    void returnMpToLobby();
    QString assetHash() const;
    QString bankLabel() const;
    int questionCountForMode(GameMode mode) const;
    // 单人与多人共用的距离上限：题库所有地点包围盒的对角线（米）。
    double questionBankMaxDistanceMeters() const;
    void selectRoundQuestions();
    void loadRound(int index);
    void refreshHud();
    void updateNotice();
    void positionNotice();
    void resumeTimerAfterOverlay();
    void submitAnswer();
    void showSummary();
    void clearSummaryRows();
    QPixmap loadScreenshot(const Question &question, int roundNumber) const;
    QPixmap makePlaceholderScreenshot(int seed, int roundNumber, const QString &caption) const;
    QString questionLabel(const Question &question) const;
    QRect mapButtonRectInWindow() const;

    ScreenshotView *m_screenshot = nullptr;
    QStackedWidget *m_viewStack = nullptr;
    QWidget *m_hud = nullptr;
    QLabel *m_roundLabel = nullptr;
    QLabel *m_topTimer = nullptr;
    QPushButton *m_submitButton = nullptr;
    QPushButton *m_mapButton = nullptr;
    QLabel *m_countdownLabel = nullptr;
    QLabel *m_pauseLabel = nullptr;
    QTimer *m_pauseTimer = nullptr;
    int m_pauseSeconds = 0;
    QString m_pausePrefix;
    bool m_mpPaused = false;
    // 房主在掉线等待期间的「结束等待」按钮
    QPushButton *m_pauseSkipButton = nullptr;
    // 主菜单「重新连接」卡片
    QWidget *m_reconnectCard = nullptr;
    QLabel *m_reconnectLabel = nullptr;
    // 房间里的颜色调色板
    QWidget *m_mpColorGrid = nullptr;
    QVector<QPushButton *> m_mpColorSwatches;
    QString m_lastRoomAddress;
    QString m_lastRoomName;
    QString m_lastHostName;
    QString m_pendingLeaveNotice;
    // 最后 15 秒的红色边缘警报
    AlarmOverlayWidget *m_alarm = nullptr;
    qint64 m_pausedRemainingMs = 0;
    QLabel *m_noticeLabel = nullptr;
    MapOverlay *m_overlay = nullptr;

    QWidget *m_summaryPage = nullptr;
    QWidget *m_startPage = nullptr;
    BackgroundCarousel *m_startBackground = nullptr;
    QLabel *m_coverCaption = nullptr;
    QLabel *m_coverIndexLabel = nullptr;
    QStackedWidget *m_startStack = nullptr;
    QWidget *m_mainMenuPage = nullptr;
    QWidget *m_singlePlayerPage = nullptr;
    QWidget *m_multiplayerPage = nullptr;
    QWidget *m_settingsPage = nullptr;
    QLabel *m_modeHintLabel = nullptr;
    QLabel *m_summaryRatingLabel = nullptr;
    QLabel *m_summaryTotalLabel = nullptr;
    QVBoxLayout *m_summaryRowsLayout = nullptr;

    QWidget *m_mpCreatePage = nullptr;
    QWidget *m_mpJoinPage = nullptr;
    QWidget *m_mpLobbyPage = nullptr;
    QLineEdit *m_mpCreateName = nullptr;
    QLineEdit *m_mpJoinName = nullptr;
    QLineEdit *m_mpJoinAddress = nullptr;
    QComboBox *m_mpModeCombo = nullptr;
    QSpinBox *m_mpRoundsSpin = nullptr;
    QSpinBox *m_mpSecondsSpin = nullptr;
    QSpinBox *m_mpPortSpin = nullptr;
    QLabel *m_mpCreateStatus = nullptr;
    QLabel *m_mpJoinStatus = nullptr;
    QLabel *m_mpLobbyPlayers = nullptr;
    QLabel *m_mpLobbyStatus = nullptr;
    QTextEdit *m_mpChatHistory = nullptr;
    QLineEdit *m_mpChatInput = nullptr;
    QPushButton *m_mpChatSendButton = nullptr;
    QPushButton *m_mpReadyButton = nullptr;
    QWidget *m_mpLobbySettingsPanel = nullptr;
    QComboBox *m_mpLobbyModeCombo = nullptr;
    QSpinBox *m_mpLobbyRoundsSpin = nullptr;
    QSpinBox *m_mpLobbySecondsSpin = nullptr;
    QPushButton *m_mpApplySettingsButton = nullptr;
    QPushButton *m_mpStartButton = nullptr;
    BattleHudWidget *m_mpHud = nullptr;
    QWidget *m_mpResultPanel = nullptr;
    QLabel *m_mpResultLabel = nullptr;
    QPushButton *m_mpContinueButton = nullptr;

    QSpinBox *m_roundsSpinBox = nullptr;
    QSpinBox *m_secondsSpinBox = nullptr;
    QCheckBox *m_timerCheckBox = nullptr;
    QCheckBox *m_fullscreenCheckBox = nullptr;
    MultiplayerSession *m_mpSession = nullptr;
    
    BgmPlayer *m_bgm = nullptr;
    QSlider *m_musicSlider = nullptr;
    QSlider *m_sfxSlider = nullptr;
    int m_musicVolume = 35;
    int m_sfxVolume = 70;

    QTimer *m_timer = nullptr;
    QVector<Question> m_pool;
    // 从 questions.nteq + media.ntep 读到的只读题库；没有题库包时为 nullptr。
    QuestionBank *m_bank = nullptr;
    QVector<Question> m_roundQuestions;
    QVector<RoundResult> m_results;

    int m_roundIndex = 0;
    int m_remainingSeconds = DEFAULT_SECONDS_PER_ROUND;
    qint64 m_roundDeadlineMs = 0;
    int m_roundsPerGame = DEFAULT_ROUNDS;
    int m_secondsPerRound = DEFAULT_SECONDS_PER_ROUND;
    bool m_timerEnabled = true;
    GameMode m_gameMode = GameMode::PhotoOnly;
    bool m_roundActive = false;
    bool m_timerPausedForOverlay = false;
    bool m_hasGuess = false;
    QPointF m_guess{0.5, 0.5};

    bool m_mpActive = false;
    bool m_mpIsHost = false;
    bool m_mpLocalSubmitted = false;
    bool m_mpRoundResolved = false;
    bool m_mpMatchEnded = false;
    int m_mpLocalId = 0;
    int m_mpRoundIndex = 0;
    int m_mpRounds = DEFAULT_ROUNDS;
    int m_mpSeconds = DEFAULT_SECONDS_PER_ROUND;
    double m_mpMultiplier = 1.0;
    QVector<MpPlayerState> m_mpState;
    QVector<QString> m_mpQuestionIds;
    QMap<int, QPointF> m_mpGuesses;
    // id -> (名字, 是否准备)；用于在大厅里生成"加入 / 离开 / 准备"系统消息。
    QHash<int, QPair<QString, bool>> m_mpLobbySnapshot;

    bool m_questionsMissing = false;
    QString m_bankError;
    QRect m_windowedGeometry;
    int m_advanceToken = 0;
};
