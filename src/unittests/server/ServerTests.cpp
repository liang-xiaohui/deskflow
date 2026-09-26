/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "../deskflow/FakePlatformScreen.h"
#include "../deskflow/MockEventQueue.h"
#include "base/IJob.h"
#include "common/Settings.h"
#include "common/VersionInfo.h"
#include "deskflow/AppUtil.h"
#include "deskflow/Screen.h"
#include "io/IStream.h"
#include "mt/Thread.h"
#include "mt/ThreadException.h"
#include "server/ClientProxy1_9.h"
#include "server/InputFilter.h"
#include "server/PrimaryClient.h"
#include "server/Server.h"
#include <QLocalSocket>
#include <QUuid>
#include <atomic>

namespace {
class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }
  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }
  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

class Stream : public deskflow::IStream
{
public:
  QByteArray output;
  void close() override
  {
  }
  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }
  void write(const void *data, uint32_t count) override
  {
    output.append(static_cast<const char *>(data), count);
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<Stream *>(this);
  }
  bool isReady() const override
  {
    return false;
  }
  uint32_t getSize() const override
  {
    return 0;
  }
};

class Events : public MockEventQueue
{
public:
  void addEvent(Event &&event) override
  {
    Event::deleteData(event);
  }
};

class TestIpcServer : public deskflow::core::ipc::IpcServer
{
public:
  explicit TestIpcServer(const QString &name) : IpcServer(nullptr, name, "test")
  {
  }

private:
  void processCommand(QLocalSocket *, const QString &, const QStringList &) override
  {
  }
};

struct Fixture
{
  Events events;
  FakePlatformScreen *platform = new FakePlatformScreen(&events, true);
  deskflow::Screen screen{platform, &events};
  PrimaryClient primary{"server", &screen};
  deskflow::server::Config config{&events};
  std::unique_ptr<Server> server;
  Stream *first = new Stream;
  Stream *second = new Stream;
  ClientProxy1_9 *firstClient = nullptr;
  ClientProxy1_9 *secondClient = nullptr;

  Fixture()
  {
    config.addScreen("server");
    config.addScreen("first");
    config.addScreen("second");
    config.getInputFilter()->addFilterRule(InputFilter::Rule(new InputFilter::KeystrokeCondition(&events, 'A', 0)));
    server = std::make_unique<Server>(config, &primary, &screen, &events);
    firstClient = new ClientProxy1_9("first", first, server.get(), &events);
    secondClient = new ClientProxy1_9("second", second, server.get(), &events);
    server->adoptClient(firstClient);
    server->adoptClient(secondClient);
    first->output.clear();
    second->output.clear();
  }
};
} // namespace

void ServerTests::initTestCase()
{
  m_arch.init();
  static TestAppUtil util;
  m_log.setFilter(LogLevel::Level::Warning);
  QVERIFY(m_settings.isValid());
  Settings::setSettingsFile(m_settings.filePath("Deskflow.conf"));
  Settings::setStateFile(m_settings.filePath("state.conf"));
  Settings::setValue(Settings::Server::KeyboardFollow, true);
  m_ipc = std::make_unique<deskflow::core::ipc::CoreIpcServer>(nullptr);
}

void ServerTests::mouseFollowRoutesOnlyTheKeyboard()
{
  Fixture f;
  QCOMPARE(f.server->m_inputFilter->getNumRules(), 0u);
  QCOMPARE(f.config.getInputFilter()->getNumRules(), 1u);
  const auto warps = f.platform->warps;
  f.server->handleKeyboardFollowRequest(f.firstClient);
  QVERIFY(f.platform->diverted);
  QCOMPARE(f.server->keyboardSink(), f.firstClient);
  QCOMPARE(f.server->m_active, &f.primary);
  f.first->output.clear();
  f.second->output.clear();
  f.server->m_keyboardBroadcasting = true;
  f.server->onKeyDown('A', 0, 30, "en", "*");
  f.server->onKeyRepeat('A', 0, 1, 30, "en");
  f.server->onKeyUp('A', 0, 30, "*");
  QVERIFY(f.first->output.contains("DKDL"));
  QVERIFY(f.second->output.isEmpty());
  f.server->switchScreen(f.secondClient, 1, 1, false);
  QCOMPARE(f.server->m_active, &f.primary);
  QCOMPARE(f.server->keyboardSink(), f.firstClient);
  f.server->onMouseMovePrimary(0, 100);
  QCOMPARE(f.server->keyboardSink(), &f.primary);
  QVERIFY(!f.platform->diverted);
  QCOMPARE(f.platform->warps, warps);
}

void ServerTests::heldModifiersMoveAndDisconnectRestoresLocal()
{
  Fixture f;
  f.server->onKeyDown(kKeyShift_L, KeyModifierShift, 42, "en", nullptr);
  f.server->handleKeyboardFollowRequest(f.firstClient);
  QVERIFY(f.first->output.contains("DKDL")); // Shift is delivered before a client mouse click.
  f.first->output.clear();
  f.server->handleKeyboardFollowRequest(f.secondClient);
  QVERIFY(f.first->output.contains(QByteArray::fromHex("444b42460001")));
  QVERIFY(f.second->output.contains("DKDL"));
  f.server->onKeyUp(kKeyShift_L, 0, 42, nullptr);
  QVERIFY(f.server->m_keyboardFollowModifiers.empty());
  QVERIFY(f.server->removeClient(f.secondClient));
  QCOMPARE(f.server->keyboardSink(), &f.primary);
  QVERIFY(!f.platform->diverted);
  delete f.secondClient;
}

void ServerTests::guiReconnectReceivesCurrentKeyboardTarget()
{
  const auto name = QString("deskflow-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  TestIpcServer server(name);
  server.listen();
  server.broadcastCommand("keyboardTarget", "first", true);
  server.broadcastCommand("keyboardTarget", "second", true);
  QLocalSocket client;
  QByteArray received;
  connect(&client, &QLocalSocket::readyRead, this, [&] { received += client.readAll(); });
  client.connectToServer(name);
  QVERIFY(client.waitForConnected(1000));
  const auto hello = QString("hello=%1+%2\n").arg(kVersion, kVersionGitSha).toUtf8();
  client.write(hello);
  client.flush();
  QTRY_VERIFY(received.contains("keyboardTarget=second\n"));
  QVERIFY(!received.contains("keyboardTarget=first\n"));
  client.disconnectFromServer();
  QTRY_COMPARE(client.state(), QLocalSocket::UnconnectedState);
  QCoreApplication::processEvents();
  received.clear();
  server.broadcastCommand("keyboardTarget", "third", true);
  client.connectToServer(name);
  QVERIFY(client.waitForConnected(1000));
  client.write(hello);
  client.flush();
  QTRY_VERIFY(received.contains("keyboardTarget=third\n"));
  QVERIFY(!received.contains("keyboardTarget=second\n"));
}

#ifdef WIN32
void ServerTests::workerCancellationCompletes()
{
  class Job : public IJob
  {
  public:
    Job(std::atomic<bool> &ready, std::atomic<bool> &cancelled) : ready(ready), cancelled(cancelled)
    {
    }
    void run() override
    {
      ready = true;
      try {
        for (int i = 0; i < 400; ++i) {
          Thread::testCancel();
          Arch::sleep(0.005);
        }
      } catch (ThreadCancelException &) {
        cancelled = true;
        throw;
      }
    }
    std::atomic<bool> &ready;
    std::atomic<bool> &cancelled;
  };
  std::atomic<bool> ready = false;
  std::atomic<bool> cancelled = false;
  Thread worker(new Job(ready, cancelled));
  QTRY_VERIFY(ready.load());
  worker.cancel();
  QVERIFY(worker.wait(3));
  QVERIFY(cancelled.load());
}
#endif

void ServerTests::SwitchToScreenInfo_alloc_screen()
{
  auto actual = new Server::SwitchToScreenInfo("test");
  QCOMPARE(actual->m_screen, "test");
  delete actual;
}

void ServerTests::KeyboardBroadcastInfo_alloc_stateAndSceens()
{
  auto info = new Server::KeyboardBroadcastInfo(Server::KeyboardBroadcastInfo::State::kOn, "test");
  QCOMPARE(info->m_state, Server::KeyboardBroadcastInfo::State::kOn);
  QCOMPARE(info->m_screens, "test");
  delete info;
}

QTEST_MAIN(ServerTests)
