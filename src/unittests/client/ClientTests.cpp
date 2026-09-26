/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientTests.h"
#include "../deskflow/FakePlatformScreen.h"
#include "../deskflow/MockEventQueue.h"
#include "client/Client.h"
#include "common/Settings.h"
#include "deskflow/AppUtil.h"
#include "deskflow/Screen.h"
#include "io/IStream.h"
#include "net/ISocketFactory.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>

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

class FakeSocketFactory : public ISocketFactory
{
public:
  IDataSocket *create(IArchNetwork::AddressFamily, SecurityLevel) const override
  {
    return nullptr;
  }
  IListenSocket *createListen(IArchNetwork::AddressFamily, SecurityLevel) const override
  {
    return nullptr;
  }
};

class Stream : public deskflow::IStream
{
public:
  QByteArray input, output;
  void close() override
  {
  }
  uint32_t read(void *buffer, uint32_t size) override
  {
    auto count = std::min<uint32_t>(size, input.size());
    if (buffer != nullptr)
      std::memcpy(buffer, input.constData(), count);
    input.remove(0, count);
    return count;
  }
  void write(const void *buffer, uint32_t size) override
  {
    output.append(static_cast<const char *>(buffer), size);
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
    return !input.isEmpty();
  }
  uint32_t getSize() const override
  {
    return input.size();
  }
};

class Events : public MockEventQueue
{
public:
  std::map<std::pair<EventTypes, void *>, EventHandler> handlers;
  std::map<EventQueueTimer *, std::unique_ptr<int>> timers;
  std::vector<Event> added;
  ~Events() override
  {
    for (const auto &event : added)
      Event::deleteData(event);
  }
  EventQueueTimer *newTimer(double, void *) override
  {
    auto storage = std::make_unique<int>();
    auto timer = reinterpret_cast<EventQueueTimer *>(storage.get());
    timers.emplace(timer, std::move(storage));
    return timer;
  }
  EventQueueTimer *newOneShotTimer(double duration, void *target) override
  {
    return newTimer(duration, target);
  }
  void deleteTimer(EventQueueTimer *timer) override
  {
    timers.erase(timer);
  }
  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    handlers[{type, target}] = handler;
  }
  void removeHandler(EventTypes type, void *target) override
  {
    handlers.erase({type, target});
  }
  void addEvent(Event &&event) override
  {
    added.push_back(std::move(event));
  }
  bool dispatchEvent(const Event &event) override
  {
    const auto it = handlers.find({event.getType(), event.getTarget()});
    if (it == handlers.end())
      return false;
    const auto callback = it->second;
    callback(event);
    return true;
  }
};

struct Fixture
{
  Events events;
  FakePlatformScreen *platform = new FakePlatformScreen(&events);
  deskflow::Screen screen{platform, &events};
  std::unique_ptr<Client> client =
      std::make_unique<Client>(&events, "client", NetworkAddress(), new FakeSocketFactory, &screen);
  Stream *stream = new Stream;
};
} // namespace

void ClientTests::initTestCase()
{
  m_arch.init();
  QVERIFY(m_settings.isValid());
  Settings::setSettingsFile(m_settings.filePath("Deskflow.conf"));
  Settings::setStateFile(m_settings.filePath("state.conf"));
  static TestAppUtil util;
  m_log.setFilter(LogLevel::Level::Warning);
}

void ClientTests::followsOnlyWhenEnabled()
{
  Fixture f;
  f.client->m_stream = f.stream;
  QVERIFY(f.client->setupScreen(9));
  f.client->handshakeComplete();
  QVERIFY(f.client->m_followTimer == nullptr);
  f.client->keyboardFollowChanged(false, true);
  QVERIFY(f.client->m_followTimer != nullptr);
  f.platform->x += 4;
  f.client->handleFollowTimer();
  QCOMPARE(f.stream->output, QByteArray::fromHex("434b424600000001"));
  f.client->keyboardFollowChanged(false, false);
  QVERIFY(f.client->m_followTimer == nullptr);
  QCOMPARE(f.platform->warps, 0);
}

void ClientTests::doesNotReclaimAfterOwnershipLoss()
{
  Fixture f;
  f.client->m_stream = f.stream;
  QVERIFY(f.client->setupScreen(9));
  f.client->handshakeComplete();
  f.client->keyboardFollowChanged(true, true);
  f.platform->x += 40;
  f.client->handleFollowTimer();
  f.platform->x += 40; // Move between a timer tick and losing the keyboard.
  f.client->keyboardFollowChanged(false, true);
  f.client->handleFollowTimer();
  QVERIFY(f.stream->output.isEmpty());
  QCOMPARE(f.platform->releases, 1);
  f.platform->x += 4;
  f.client->handleFollowTimer();
  QCOMPARE(f.stream->output.count("CKBF"), 1);
}

void ClientTests::cooldownExpiresWhileStationary()
{
  Fixture f;
  f.client->m_stream = f.stream;
  QVERIFY(f.client->setupScreen(9));
  f.client->handshakeComplete();
  f.client->keyboardFollowChanged(false, true);
  f.platform->x += 4;
  f.client->handleFollowTimer();
  for (int i = 0; i < 6; ++i)
    f.client->handleFollowTimer();
  f.platform->x += 4;
  f.client->handleFollowTimer();
  QCOMPARE(f.stream->output.count("CKBF"), 2);
}

void ClientTests::cleanupReleasesKeysAndTimer_data()
{
  QTest::addColumn<int>("path");
  QTest::newRow("disconnect") << 0;
  QTest::newRow("output error") << 1;
  QTest::newRow("destruction") << 2;
}

void ClientTests::cleanupReleasesKeysAndTimer()
{
  QFETCH(int, path);
  Fixture f;
  f.client->m_stream = f.stream;
  QVERIFY(f.client->setupScreen(9));
  f.client->handshakeComplete();
  f.client->keyboardFollowChanged(true, true);
  auto *timer = f.client->m_followTimer;
  QVERIFY(timer != nullptr);
  if (path == 0)
    f.client->handleDisconnected();
  else if (path == 1)
    f.client->handleOutputError();
  else
    f.client.reset();
  QVERIFY(!f.events.dispatchEvent(Event(EventTypes::Timer, timer)));
  QVERIFY(f.events.timers.empty());
  QCOMPARE(f.platform->releases, 1);
  QVERIFY(!f.platform->localCursor);
}

void ClientTests::reconnectToOlderServer()
{
  Fixture f;
  f.client->m_stream = f.stream;
  QVERIFY(f.client->setupScreen(9));
  f.client->handshakeComplete();
  f.client->keyboardFollowChanged(true, true);
  f.client->cleanupScreen();
  QVERIFY(f.client->setupScreen(8));
  f.client->handshakeComplete();
  QVERIFY(!f.client->m_keyboardFollowSupported);
  QVERIFY(!f.client->m_keyboardFollowMode);
  QVERIFY(f.client->m_followTimer == nullptr);
}

void ClientTests::protocolStateBeforeAndAfterHandshake()
{
  Fixture f;
  f.client->m_stream = f.stream;
  QVERIFY(f.client->setupScreen(9));
  f.stream->input = QByteArray::fromHex("444b42460001");
  QVERIFY(f.events.dispatchEvent(Event(EventTypes::StreamInputReady, f.stream)));
  QVERIFY(f.platform->localCursor);
  QVERIFY(f.client->m_followTimer == nullptr);
  // DSOP with zero options completes the real proxy handshake.
  f.stream->input = QByteArray::fromHex("44534f5000000000");
  QVERIFY(f.events.dispatchEvent(Event(EventTypes::StreamInputReady, f.stream)));
  QVERIFY(f.client->m_ready);
  QVERIFY(f.client->m_followTimer != nullptr);
  f.stream->input = QByteArray::fromHex("444b42460101");
  QVERIFY(f.events.dispatchEvent(Event(EventTypes::StreamInputReady, f.stream)));
  QVERIFY(f.client->m_isKeyboardFollowTarget);
  f.stream->input = QByteArray::fromHex("444b42460001");
  QVERIFY(f.events.dispatchEvent(Event(EventTypes::StreamInputReady, f.stream)));
  QCOMPARE(f.platform->releases, 1);
}

QTEST_MAIN(ClientTests)
