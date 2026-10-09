#include "common/Settings.h"
#include "gui/core/FlexbarManager.h"
#include "gui/dialogs/SettingsDialog.h"
#include "gui/widgets/FlexbarSettingsWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLocalServer>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtTest>
#include <iostream>

using deskflow::gui::FlexbarManager;

static QByteArray frame(bool running = true)
{
  return QJsonDocument(
             QJsonObject{
                 {"type", "status"},
                 {"version", 1},
                 {"running", running},
                 {"connected", running},
                 {"phase", running ? "ready" : "disabled"},
                 {"owner", "windows"},
                 {"pageActive", running},
                 {"actionsGranted", running},
                 {"pending", 0},
                 {"lastIssue", QJsonValue::Null}
             }
         ).toJson(QJsonDocument::Compact) +
         '\n';
}

class FlexbarTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    QVERIFY(m_directory.isValid());
    Settings::setSettingsFile(m_directory.filePath("Deskflow.conf"));
    Settings::setStateFile(m_directory.filePath("state.conf"));
    Settings::setValue(Settings::Security::TlsEnabled, false);
    Settings::setValue(Settings::Gui::AutoUpdateCheck, false);
  }

  void settingsSaveCancelReset()
  {
    QCOMPARE(Settings::defaultValue(Settings::Flexbar::Enabled).toBool(), false);
    QVERIFY(Settings::validKeys().contains(Settings::Flexbar::Enabled));
    Settings::setValue(Settings::Flexbar::Enabled, false);
    ServerConfig config;
    SettingsDialog cancelled(nullptr, config);
    auto *enabled = cancelled.findChild<QCheckBox *>("cbFlexbarEnabled");
    QVERIFY(enabled);
    enabled->setChecked(true);
    cancelled.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
    QVERIFY(!Settings::value(Settings::Flexbar::Enabled).toBool());
    SettingsDialog saved(nullptr, config);
    saved.findChild<QCheckBox *>("cbFlexbarEnabled")->setChecked(true);
    saved.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    QVERIFY(!saved.requiresCoreRestart());
    Settings::save(false);
    QSettings disk(Settings::settingsFile(), QSettings::IniFormat);
    QVERIFY(disk.value(Settings::Flexbar::Enabled).toBool());
    SettingsDialog restored(nullptr, config);
    auto *widget = restored.findChild<deskflow::gui::FlexbarSettingsWidget *>();
    QVERIFY(restored.findChild<QCheckBox *>("cbFlexbarEnabled")->isChecked());
    widget->load(true);
    QVERIFY(!restored.findChild<QCheckBox *>("cbFlexbarEnabled")->isChecked());
    QVERIFY(widget->isModified());
    restored.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Reset)->click();
    QVERIFY(restored.findChild<QCheckBox *>("cbFlexbarEnabled")->isChecked());
    const auto screenshots = qEnvironmentVariable("DESKFLOW_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
      restored.findChild<QTabWidget *>()->setCurrentWidget(widget);
      restored.show();
      QCoreApplication::processEvents();
      QVERIFY(restored.grab().save(screenshots + "/flexbar-settings.png"));
    }
    Settings::setValue(Settings::Flexbar::Enabled, false);
  }

  void onlyRealNonFlexbarEditsRestartCore()
  {
    ServerConfig config;
    SettingsDialog reverted(nullptr, config);
    auto *checkbox = reverted.findChild<QCheckBox *>("cbPreventSleep");
    QVERIFY(checkbox);
    const auto original = checkbox->isChecked();
    checkbox->setChecked(!original);
    checkbox->setChecked(original);
    reverted.findChild<QCheckBox *>("cbFlexbarEnabled")->setChecked(true);
    reverted.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    QVERIFY(!reverted.requiresCoreRestart());
    SettingsDialog changed(nullptr, config);
    changed.findChild<QCheckBox *>("cbPreventSleep")->setChecked(!original);
    changed.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    QVERIFY(changed.requiresCoreRestart());
    Settings::setValue(Settings::Core::PreventSleep, original);
    Settings::setValue(Settings::Flexbar::Enabled, false);
  }

  void localBridgeEnableStopReconnect()
  {
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    const auto endpoint = m_directory.filePath("bridge.sock");
    QVERIFY(server.listen(endpoint));
#ifdef Q_OS_UNIX
    QVERIFY(QFile::setPermissions(endpoint, QFile::ReadOwner | QFile::WriteOwner));
#endif
    FlexbarManager manager;
    FlexbarManager::Options options;
    options.socketPath = endpoint;
    manager.configure(false, options);
    QTest::qWait(50);
    QVERIFY(!server.hasPendingConnections());
    manager.configure(true, options);
    QTRY_VERIFY(server.hasPendingConnections());
    auto *peer = server.nextPendingConnection();
    QTRY_VERIFY(peer->bytesAvailable());
    QCOMPARE(QJsonDocument::fromJson(peer->readLine()).object().value("type"), "start");
    peer->write(frame());
    QTRY_VERIFY(manager.status().value("running").toBool());
    manager.configure(true, options);
    QVERIFY(!server.hasPendingConnections());
    manager.configure(false, options);
    QTRY_VERIFY(peer->bytesAvailable());
    QCOMPARE(QJsonDocument::fromJson(peer->readLine()).object().value("type"), "stop");
    peer->write(frame(false));
    QTRY_COMPARE(peer->state(), QLocalSocket::UnconnectedState);
    QVERIFY(!manager.enabled());
    QVERIFY(manager.status().isEmpty());
    manager.configure(true, options);
    QTRY_VERIFY(server.hasPendingConnections());
    auto *second = server.nextPendingConnection();
    manager.shutdown();
    QTRY_COMPARE(second->state(), QLocalSocket::UnconnectedState);
  }

  void rejectsInvalidResponse()
  {
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    const auto endpoint = m_directory.filePath("invalid.sock");
    QVERIFY(server.listen(endpoint));
#ifdef Q_OS_UNIX
    QVERIFY(QFile::setPermissions(endpoint, QFile::ReadOwner | QFile::WriteOwner));
#endif
    FlexbarManager manager;
    FlexbarManager::Options options;
    options.socketPath = endpoint;
    manager.configure(true, options);
    QTRY_VERIFY(server.hasPendingConnections());
    auto *peer = server.nextPendingConnection();
    peer->write("{\"type\":\"status\",\"version\":2}\n");
    QTRY_VERIFY(manager.statusText().contains("Invalid"));
    QVERIFY(manager.status().isEmpty());
    peer->disconnectFromServer();
    QTRY_COMPARE(peer->state(), QLocalSocket::UnconnectedState);
    QTRY_VERIFY(manager.statusText().contains("Invalid"));
  }

  void childProcessOwnershipAndRestart()
  {
    FlexbarManager manager;
    FlexbarManager::Options options;
    options.transport = FlexbarManager::Options::Transport::ChildProcess;
    options.program = QCoreApplication::applicationFilePath();
    options.arguments = {options.program, "--flexbar-fixture", m_directory.filePath("stops")};
    manager.configure(true, options);
    QTRY_VERIFY_WITH_TIMEOUT(manager.status().value("running").toBool(), 3000);
    manager.restart();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(m_directory.filePath("stops")), 3000);
    // The old status can remain visible while restart drains the first child.
    QTRY_VERIFY_WITH_TIMEOUT(
        manager.status().value("running").toBool() && manager.statusText().contains("Running"), 3000
    );
    manager.configure(false, options);
    QTRY_VERIFY_WITH_TIMEOUT(manager.status().isEmpty(), 3000);
    // Wait for the owned child to finish writing its exit marker.
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
          QFile marker(m_directory.filePath("stops"));
          return marker.open(QIODevice::ReadOnly) && marker.readAll() == QByteArray("stop\nstop\n");
        }(),
        3000
    );
    QFile stopped(m_directory.filePath("stops"));
    QVERIFY(stopped.open(QIODevice::ReadOnly));
    QCOMPARE(stopped.readAll(), QByteArray("stop\nstop\n"));
  }

  void missingPackageDoesNotLaunch()
  {
    FlexbarManager manager;
    FlexbarManager::Options options;
    options.transport = FlexbarManager::Options::Transport::ChildProcess;
    options.program = m_directory.filePath("missing");
    manager.configure(true, options);
    QVERIFY(manager.status().isEmpty());
    QVERIFY(manager.statusText().contains("not installed"));
  }

  void cancelWhileChildStarts()
  {
    FlexbarManager manager;
    FlexbarManager::Options options;
    options.transport = FlexbarManager::Options::Transport::ChildProcess;
    options.program = QCoreApplication::applicationFilePath();
    options.arguments = {options.program, "--flexbar-fixture", m_directory.filePath("cancel-stop")};
    manager.configure(true, options);
    manager.configure(false, options);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(m_directory.filePath("cancel-stop")), 3000);
    QTRY_VERIFY(manager.status().isEmpty());
  }

  void chineseSettingsPreview()
  {
    const auto translation = qEnvironmentVariable("DESKFLOW_TEST_TRANSLATION");
    if (translation.isEmpty())
      return;
    QTranslator translator;
    QVERIFY(translator.load(translation));
    QCoreApplication::installTranslator(&translator);
    FlexbarManager manager;
    ServerConfig config;
    SettingsDialog dialog(nullptr, config, &manager);
    auto *widget = dialog.findChild<deskflow::gui::FlexbarSettingsWidget *>();
    QCOMPARE(dialog.findChild<QCheckBox *>("cbFlexbarEnabled")->text(), QString::fromUtf8("启用 Flexbar"));
    dialog.findChild<QTabWidget *>()->setCurrentWidget(widget);
    dialog.show();
    QCoreApplication::processEvents();
    const auto screenshots = qEnvironmentVariable("DESKFLOW_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty())
      QVERIFY(dialog.grab().save(screenshots + "/flexbar-settings-zh.png"));
    QCoreApplication::removeTranslator(&translator);
  }

  void unresponsiveChildStopsInsteadOfRestarting()
  {
    FlexbarManager manager;
    FlexbarManager::Options options;
    options.transport = FlexbarManager::Options::Transport::ChildProcess;
    options.program = QCoreApplication::applicationFilePath();
    options.arguments = {options.program, "--flexbar-silent", m_directory.filePath("timeout-stop")};
    manager.configure(true, options);
    QTRY_VERIFY(manager.status().value("running").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(m_directory.filePath("timeout-stop")), 9000);
    QTRY_VERIFY(manager.statusText().contains("stopped responding"));
    QVERIFY(manager.status().isEmpty());
  }

private:
  QTemporaryDir m_directory;
};

int main(int argc, char **argv)
{
  const bool silent = argc == 4 && QString::fromLocal8Bit(argv[2]) == QStringLiteral("--flexbar-silent");
  if (argc == 4 && (silent || QString::fromLocal8Bit(argv[2]) == QStringLiteral("--flexbar-fixture"))) {
    std::cout << frame().constData() << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.find("stop") != std::string::npos)
        break;
      if (!silent)
        std::cout << frame().constData() << std::flush;
    }
    QFile marker(QString::fromLocal8Bit(argv[3]));
    if (!marker.open(QIODevice::Append))
      return 3;
    marker.write("stop\n");
    marker.close();
    std::cout << frame(false).constData() << std::flush;
    return 0;
  }
  QApplication application(argc, argv);
  FlexbarTests tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "FlexbarTests.moc"
