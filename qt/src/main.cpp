#include "GameWindow.h"
#include "Logging.h"
#include "PortableSettings.h"
#include "SoundPlayer.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QFont>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NTE SceneSeeker"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("NTE SceneSeeker"));

    // --profile X：同一台电脑多开测试用。每个实例使用独立的存档目录
    // （clientId / 名字 / 窗口状态互不影响），否则会共用同一份 QSettings。
    // 必须在第一次使用 QSettings / Logging 之前生效。
    {
        const QStringList args = app.arguments();
        QString profile;
        for (int i = 1; i < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--profile") && i + 1 < args.size()) {
                profile = args.at(i + 1).trimmed();
            } else if (args.at(i).startsWith(QStringLiteral("--profile="))) {
                profile = args.at(i).section(QLatin1Char('='), 1).trimmed();
            }
        }
        if (!profile.isEmpty()) {
            PortableSettings::profileSuffix() = profile;
            QApplication::setApplicationName(
                QStringLiteral("NTE SceneSeeker %1").arg(profile));
        }
    }

    // 绿色版：设置/存档都放 exe 旁边。老版本存在注册表里的，第一次运行搬过来。
    {
        const QString ini = PortableSettings::filePath();
        if (!QFileInfo::exists(ini) && PortableSettings::profileSuffix().isEmpty()) {
            QSettings legacy;   // 默认格式 = 注册表
            const QStringList keys = legacy.allKeys();
            if (!keys.isEmpty()) {
                QSettings fresh(ini, QSettings::IniFormat);
                for (const QString &key : keys)
                    fresh.setValue(key, legacy.value(key));
                fresh.sync();
            }
        }
    }

    Logging::install();
    Sfx::init();

    if (QStyle *style = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(style);

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#05070a")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#d9e7ec")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#0b0f14")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#111820")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#d9e7ec")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#101820")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#d9e7ec")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#69e0ff")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#04141b")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#0b1117")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#d9e7ec")));
    QApplication::setPalette(palette);

    QFont font = QApplication::font();
    font.setFamily(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSize(10);
    QApplication::setFont(font);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("异环图寻 · NTE SceneSeeker"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption windowedOption(
        QStringList{QStringLiteral("w"), QStringLiteral("windowed")},
        QStringLiteral("以窗口模式启动（默认全屏）。"));
    const QCommandLineOption smokeOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("启动约 1.8 秒后自动退出，用于 offscreen 冒烟测试。"));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("启动约 1.2 秒后保存窗口截图并退出（开发自检用）。"),
        QStringLiteral("path"));
    const QCommandLineOption autoMapOption(
        QStringLiteral("auto-map"),
        QStringLiteral("启动后自动打开地图覆盖层（开发自检用）。"));
    const QCommandLineOption autoPlayOption(
        QStringLiteral("auto-play"),
        QStringLiteral("启动后自动进入第一回合（开发自检用）。"));
    const QCommandLineOption autoReviewOption(
        QStringLiteral("auto-review"),
        QStringLiteral("配合 --auto-play：自动落点并提交，用于查看结算回放（开发自检用）。"));
    const QCommandLineOption autoMpResultOption(
        QStringLiteral("auto-mp-result"),
        QStringLiteral("直接播一次假的多人回合结算（开发自检用）。"));
    const QCommandLineOption autoMpEndOption(
        QStringLiteral("auto-mp-end"),
        QStringLiteral("直接显示假的「比赛结束」面板（开发自检用）。"));
    const QCommandLineOption profileOption(
        QStringLiteral("profile"),
        QStringLiteral("同一台电脑多开测试用：给这个实例独立的存档目录（clientId/名字/窗口状态）。"),
        QStringLiteral("name"));
    const QCommandLineOption autoReconnectOption(
        QStringLiteral("auto-reconnect-prompt"),
        QStringLiteral("直接显示主菜单的「重新连接」卡片（开发自检用）。"));
    const QCommandLineOption autoHudOption(
        QStringLiteral("auto-hud"),
        QStringLiteral("造 4 名玩家只显示顶部血条（开发自检用）。"));
    const QCommandLineOption autoPauseOption(
        QStringLiteral("auto-pause"),
        QStringLiteral("装作房主遇到掉线，显示等待遮罩 + 结束等待按钮（开发自检用）。"));
    const QCommandLineOption autoLobbyOption(
        QStringLiteral("auto-lobby"),
        QStringLiteral("直接开一个本地房间并停在房间界面（开发自检用）。"));
    parser.addOption(windowedOption);
    parser.addOption(smokeOption);
    parser.addOption(screenshotOption);
    parser.addOption(autoMapOption);
    parser.addOption(autoPlayOption);
    parser.addOption(autoReviewOption);
    parser.addOption(autoMpResultOption);
    parser.addOption(autoMpEndOption);
    parser.addOption(profileOption);
    parser.addOption(autoReconnectOption);
    parser.addOption(autoHudOption);
    parser.addOption(autoLobbyOption);
    parser.addOption(autoPauseOption);
    parser.process(app);

    GameWindow window;
    if (parser.isSet(windowedOption)) {
        const QByteArray saved = PortableSettings::appSettings()->value(QStringLiteral("game/windowGeometry")).toByteArray();
        // 同一台电脑多开：--profile 末尾是数字时按槽位错开窗口，避免窗口完全重叠。
        int slot = -1;
        {
            const QString name = parser.value(profileOption).trimmed();
            if (!name.isEmpty() && name.at(name.size() - 1).isDigit())
                slot = name.at(name.size() - 1).digitValue() - 1;
        }
        if (saved.isEmpty() || !window.restoreGeometry(saved)) {
            if (slot >= 0) {
                window.resize(1000, 660);
                window.move(60 + slot * 110, 50 + slot * 80);
            } else {
                window.resize(1440, 900);
            }
        }
        window.show();
    } else {
        window.showWithSavedState();
    }

    if (parser.isSet(autoPlayOption))
        QTimer::singleShot(300, &window, &GameWindow::startGameForTesting);

    if (parser.isSet(autoMapOption))
        QTimer::singleShot(500, &window, &GameWindow::openMapForPick);

    if (parser.isSet(autoReviewOption))
        QTimer::singleShot(1200, &window, &GameWindow::submitForTesting);

    if (parser.isSet(autoMpResultOption)) {
        QTimer::singleShot(600, &window, &GameWindow::showFakeMpRoundForTesting);
        // 再来一次：检查上一回合的卡片/浮动数字有没有清干净。
        QTimer::singleShot(5200, &window, &GameWindow::showFakeMpRoundForTesting);
    }

    if (parser.isSet(autoMpEndOption))
        QTimer::singleShot(600, &window, &GameWindow::showFakeMpMatchEndForTesting);

    if (parser.isSet(autoReconnectOption))
        QTimer::singleShot(600, &window, &GameWindow::showReconnectPromptForTesting);

    if (parser.isSet(autoHudOption))
        QTimer::singleShot(600, &window, &GameWindow::showHudForTesting);

    if (parser.isSet(autoLobbyOption))
        QTimer::singleShot(600, &window, &GameWindow::showFakeLobbyForTesting);

    if (parser.isSet(autoPauseOption))
        QTimer::singleShot(600, &window, &GameWindow::showFakePauseForTesting);

    if (parser.isSet(smokeOption)) {
        QTimer::singleShot(1800, &app, &QCoreApplication::quit);
    } else if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        int delay = parser.isSet(autoPlayOption) ? 5000 : 1200;
        if (parser.isSet(autoReviewOption))
            delay = 4200;
        if (parser.isSet(autoMpResultOption))
            delay = 8200;
        if (parser.isSet(autoMpEndOption))
            delay = 1600;
        if (parser.isSet(autoReconnectOption))
            delay = 2000;
        if (parser.isSet(autoHudOption))
            delay = 2000;
        if (parser.isSet(autoLobbyOption))
            delay = 2000;
        if (parser.isSet(autoPauseOption))
            delay = 1800;
        QTimer::singleShot(delay, &app, [&window, path]() {
            window.grab().save(path);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
