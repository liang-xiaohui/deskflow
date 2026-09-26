// SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
// SPDX-License-Identifier: MIT
#include "InputModeTests.h"
#include "common/Settings.h"
#include "gui/MainWindow.h"
#include "gui/StyleUtils.h"
#include "gui/config/ServerConfig.h"
#include "gui/core/CoreProcess.h"
#include "gui/dialogs/ServerConfigDialog.h"
#include "gui/ipc/DaemonIpcClient.h"
#include "gui/widgets/StatusBar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QUuid>

void InputModeTests::initTestCase()
{
  QVERIFY(m_settings.isValid());
  Settings::setSettingsFile(m_settings.filePath("Deskflow.conf"));
  Settings::setStateFile(m_settings.filePath("state.conf"));
  deskflow::gui::updateIconTheme();
  const auto translation = qEnvironmentVariable("DESKFLOW_TEST_TRANSLATION");
  if (!translation.isEmpty()) {
    QVERIFY(m_translator.load(translation));
    QCoreApplication::installTranslator(&m_translator);
  }
  const auto font = qEnvironmentVariable("DESKFLOW_TEST_FONT");
  if (!font.isEmpty()) {
    const auto id = QFontDatabase::addApplicationFont(font);
    QVERIFY(id >= 0);
    QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(), 9));
  }
}

void InputModeTests::init()
{
  Settings::setValue(Settings::Core::ComputerName, "server");
  Settings::setValue(Settings::Core::CoreMode, QVariant::fromValue(Settings::CoreMode::Server));
  Settings::setValue(Settings::Security::TlsEnabled, false);
  Settings::setValue(Settings::Gui::AutoStartCore, false);
  Settings::setValue(Settings::Gui::AutoUpdateCheck, false);
  Settings::setValue(Settings::Core::ProcessMode, Settings::ProcessMode::Service);
  Settings::setValue(Settings::Server::KeyboardFollow, false);
  Settings::setValue(Settings::Server::ExternalConfig, false);
  Settings::setValue(Settings::Server::RelativeMouseMoves, true);
  Settings::save();
}

void InputModeTests::saveAndRestore()
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
  QSKIP("Mouse follow controls require the Windows or macOS input backend.");
#endif
  ServerConfig config;
  config.addClient("client");
  ServerConfigDialog dialog(nullptr, config);
  auto *mode = dialog.findChild<QComboBox *>("comboInputMode");
  auto *switching = dialog.findChild<QGroupBox *>("groupSwitch");
  auto *tabs = dialog.findChild<QTabWidget *>("tabWidget");
  auto *relative = dialog.findChild<QCheckBox *>("cbRelativeMouseMoves");
  QVERIFY(mode && switching && tabs && relative);
  QCOMPARE(mode->count(), 2);
  QCOMPARE(mode->currentIndex(), 0);
  QCOMPARE(mode->itemText(0), QCoreApplication::translate("ServerConfigDialog", "Extended screen mode"));
  QCOMPARE(mode->itemText(1), QCoreApplication::translate("ServerConfigDialog", "Mouse follow mode"));
  dialog.show();
  QCoreApplication::processEvents();
  const auto screenshots = qEnvironmentVariable("DESKFLOW_TEST_SCREENSHOTS");
  if (!screenshots.isEmpty())
    QVERIFY(dialog.grab().save(screenshots + "/extended-screen.png"));
  mode->setCurrentIndex(1);
  QVERIFY(!switching->isEnabled());
  QVERIFY(!tabs->isTabEnabled(1));
  QVERIFY(!relative->isEnabled());
  QVERIFY(relative->isChecked()); // Preserve existing extended-screen preferences.
  QCoreApplication::processEvents();
  if (!screenshots.isEmpty())
    QVERIFY(dialog.grab().save(screenshots + "/mouse-follow.png"));
  dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
  QCOMPARE(dialog.result(), QDialog::Accepted);
  QVERIFY(Settings::value(Settings::Server::KeyboardFollow).toBool());
  QVERIFY(Settings::value(Settings::Server::RelativeMouseMoves).toBool());

  ServerConfigDialog reopened(nullptr, config);
  QCOMPARE(reopened.findChild<QComboBox *>("comboInputMode")->currentIndex(), 1);
  reopened.findChild<QComboBox *>("comboInputMode")->setCurrentIndex(0);
  QVERIFY(reopened.findChild<QGroupBox *>("groupSwitch")->isEnabled());
  QVERIFY(reopened.findChild<QTabWidget *>("tabWidget")->isTabEnabled(1));
  reopened.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
  QVERIFY(!Settings::value(Settings::Server::KeyboardFollow).toBool());
}

void InputModeTests::cancelAndReset()
{
  ServerConfig config;
  ServerConfigDialog dialog(nullptr, config);
  auto *mode = dialog.findChild<QComboBox *>("comboInputMode");
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  mode->setCurrentIndex(1);
  buttons->button(QDialogButtonBox::Reset)->click();
  QCOMPARE(mode->currentIndex(), 0);
  mode->setCurrentIndex(1);
  buttons->button(QDialogButtonBox::Cancel)->click();
  QCOMPARE(dialog.result(), QDialog::Rejected);
  QVERIFY(!Settings::value(Settings::Server::KeyboardFollow).toBool());
}

void InputModeTests::keyboardStatusAndIcons()
{
  StatusBar bar;
  auto *target = bar.findChild<QLabel *>("lblKeyboardTarget");
  QVERIFY(target);
  QVERIFY(target->isHidden());
  bar.setKeyboardTarget("client<b>");
  QCOMPARE(target->textFormat(), Qt::PlainText);
  QCOMPARE(target->text(), QCoreApplication::translate("StatusBar", "Keyboard → %1").arg("client<b>"));
  QVERIFY(!target->isHidden());
  bar.setKeyboardTarget({});
  QVERIFY(target->isHidden());
  QVERIFY(!QIcon(":/deskflow.ico").pixmap(32).isNull());
  QVERIFY(!QIcon(":/icons/deskflow-dark/apps/64/org.deskflow.deskflow.svg").pixmap(32).isNull());
}

void InputModeTests::serviceMismatchStopsStarting()
{
  using deskflow::core::ProcessState;
  using deskflow::gui::CoreProcess;
  ServerConfig config;
  CoreProcess core(config);
  QSignalSpy errors(&core, &CoreProcess::error);
  core.setProcessState(ProcessState::Starting);
  core.m_keyboardTarget = "old-target";
  Q_EMIT core.m_daemonIpcClient->versionMismatch();
  QCOMPARE(core.processState(), ProcessState::Stopped);
  QVERIFY(core.keyboardTarget().isEmpty());
  QCOMPARE(errors.count(), 1);
  QCOMPARE(errors.first().first().value<CoreProcess::Error>(), CoreProcess::Error::ServiceVersionMismatch);
  core.setProcessState(ProcessState::Starting);
  core.startProcessFromDaemon(); // A subsequent start must also reject the incompatible service.
  QCOMPARE(core.processState(), ProcessState::Stopped);
  QCOMPARE(errors.count(), 2);
}

void InputModeTests::bundledCoreRecoveryStopsServiceAndCanBeCancelled()
{
  using deskflow::core::ProcessState;
  using deskflow::gui::CoreProcess;
  ServerConfig config;
  CoreProcess core(config);
  QVERIFY(!core.useBundledCore()); // A normal service is not replaced.
  const auto socketName = "deskflow-test-" + QUuid::createUuid().toString(QUuid::Id128);
  QLocalServer daemon;
  QVERIFY(daemon.listen(socketName));
  QByteArray received;
  QLocalSocket *socket = nullptr;
  connect(&daemon, &QLocalServer::newConnection, &daemon, [&] {
    socket = daemon.nextPendingConnection();
    connect(socket, &QLocalSocket::readyRead, &daemon, [&] {
      received += socket->readAll();
      if (received.startsWith("hello=") && !received.contains("stop\n")) {
        socket->write("versionMismatch=old-service\n");
      }
    });
  });
  delete core.m_daemonIpcClient;
  core.m_daemonIpcClient = new deskflow::gui::ipc::DaemonIpcClient(&core, socketName);
  core.m_daemonIpcClient->connectToServer();
  QTRY_VERIFY(core.m_daemonIpcClient->isConnected());
  core.m_daemonVersionMismatch = true;
  core.m_appPath = QCoreApplication::applicationFilePath();
  QVERIFY(core.useBundledCore());
  QCOMPARE(Settings::value(Settings::Core::ProcessMode).value<Settings::ProcessMode>(), Settings::ProcessMode::Desktop);
  QCOMPARE(core.processState(), ProcessState::RetryPending);
  core.stop(); // Cancel before the queued desktop start; do not launch a second process.
  QCOMPARE(core.processState(), ProcessState::Stopped);
  QTRY_VERIFY(received.contains("stop\n"));
  QVERIFY(!received.contains("start\n"));
  QCOMPARE(core.processState(), ProcessState::Stopped);
  QVERIFY(!core.m_process);
  QCOMPARE(core.m_lastProcessMode.value(), Settings::ProcessMode::Desktop);
}

void InputModeTests::mainWindowModeAndTray()
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
  QSKIP("Mouse follow controls require the Windows or macOS input backend.");
#endif
  MainWindow window;
  auto *mode = window.findChild<QComboBox *>("comboSharingMode");
  auto *tray = window.findChild<QSystemTrayIcon *>();
  QVERIFY(mode && tray);
  QCOMPARE(mode->currentIndex(), 0);
  mode->setCurrentIndex(1);
  QVERIFY(Settings::value(Settings::Server::KeyboardFollow).toBool());
  QVERIFY(!tray->icon().pixmap(32).isNull());
  QVERIFY(!window.windowIcon().pixmap(32).isNull());
  Settings::setValue(Settings::Server::KeyboardFollow, false);
  QCOMPARE(mode->currentIndex(), 0);
  Settings::setValue(Settings::Server::KeyboardFollow, true);
  QCOMPARE(mode->currentIndex(), 1);
  const auto screenshots = qEnvironmentVariable("DESKFLOW_TEST_SCREENSHOTS");
  if (!screenshots.isEmpty())
    QVERIFY(window.grab().save(screenshots + "/main-window.png"));
}

QTEST_MAIN(InputModeTests)
