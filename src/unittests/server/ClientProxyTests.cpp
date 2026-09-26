/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientProxyTests.h"

#include "../deskflow/MockEventQueue.h"
#include "base/Event.h"
#include "deskflow/AppUtil.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/ClientProxy1_1.h"
#include "server/ClientProxy1_6.h"
#include "server/ClientProxy1_7.h"
#include "server/ClientProxy1_8.h"
#include "server/ClientProxy1_9.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QTest>

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

class CapturingStream : public deskflow::IStream
{
public:
  QByteArray take()
  {
    auto bytes = m_buffer;
    m_buffer.clear();
    return bytes;
  }

  //! Queue bytes as if the peer had sent them
  void feed(const QByteArray &bytes)
  {
    m_incoming.append(bytes);
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_buffer.append(static_cast<const char *>(buffer), n);
  }

  void close() override
  {
  }

  uint32_t read(void *buffer, uint32_t n) override
  {
    const auto available = static_cast<uint32_t>(m_incoming.size());
    const uint32_t count = std::min(n, available);
    if (count > 0) {
      std::memcpy(buffer, m_incoming.constData(), count);
      m_incoming.remove(0, static_cast<int>(count));
    }
    return count;
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
    return const_cast<CapturingStream *>(this);
  }

  bool isReady() const override
  {
    return false;
  }

  uint32_t getSize() const override
  {
    return 0;
  }

private:
  QByteArray m_buffer;
  QByteArray m_incoming;
};

std::unique_ptr<ClientProxy> makeProxy(int minor, deskflow::IStream *stream, IEventQueue *events)
{
  // the 1.4 and later constructors assert the server pointer is non-null but only store it
  auto *server = reinterpret_cast<Server *>(0x1);

  std::unique_ptr<ClientProxy> proxy;
  switch (minor) {
  case 0:
    proxy = std::make_unique<ClientProxy1_0>("client", stream, events);
    break;
  case 1:
    proxy = std::make_unique<ClientProxy1_1>("client", stream, events);
    break;
  case 6:
    proxy = std::make_unique<ClientProxy1_6>("client", stream, server, events);
    break;
  case 7:
    proxy = std::make_unique<ClientProxy1_7>("client", stream, server, events);
    break;
  case 8:
    proxy = std::make_unique<ClientProxy1_8>("client", stream, server, events);
    break;
  case 9:
    proxy = std::make_unique<ClientProxy1_9>("client", stream, server, events);
    break;
  default:
    break;
  }
  return proxy;
}

//! An event queue that remembers which events were posted
class RecordingEventQueue : public MockEventQueue
{
public:
  void addEvent(Event &&event) override
  {
    m_types.push_back(event.getType());
  }

  size_t count(EventTypes type) const
  {
    return static_cast<size_t>(std::count(m_types.begin(), m_types.end(), type));
  }

private:
  std::vector<EventTypes> m_types;
};

struct ProxyUnderTest
{
  MockEventQueue events;
  CapturingStream *stream = new CapturingStream;
  std::unique_ptr<ClientProxy> proxy;

  explicit ProxyUnderTest(int minor) : proxy(makeProxy(minor, stream, &events))
  {
    // drop the query-info and layout-sync messages the constructors send
    stream->take();
  }
};

//! Same as ProxyUnderTest but keeps the posted events around
struct FollowProxyUnderTest
{
  RecordingEventQueue events;
  CapturingStream *stream = new CapturingStream;
  std::unique_ptr<ClientProxy> proxy = makeProxy(9, stream, &events);

  FollowProxyUnderTest()
  {
    // drop the query-info and layout-sync messages the constructors send
    stream->take();
  }

  ClientProxy1_9 *asFollowProxy() const
  {
    return static_cast<ClientProxy1_9 *>(proxy.get());
  }
};

const KeyID kKey = 0x61;
const KeyModifierMask kMask = 0;
const KeyButton kButton = 0x1e;
const int32_t kCount = 3;
const std::string kLang = "en";

} // namespace

void ClientProxyTests::initTestCase()
{
  // the 1.8 constructor reads the keyboard layouts through AppUtil::instance()
  static TestAppUtil appUtil;
}

// These formats are frozen because shipped third-party clients parse them byte
// for byte: Synergy 1.4 through 1.14.1 negotiate 1.4 through 1.7, Barrier and
// Input Leap negotiate 1.6, and Synergy 1.14.2 onwards and Deskflow negotiate 1.8.
void ClientProxyTests::keyDown_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKDN" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKDL" + QByteArray::fromHex("0061 0000 001e 00000002") + "en";
}

void ClientProxyTests::keyDown()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyDown(kKey, kMask, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyRepeat_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKRP" + QByteArray::fromHex("0061 0000 0003");
  QTest::newRow("1.1") << 1 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.6") << 6 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.7") << 7 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.8") << 8 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e 00000002") + "en";
}

void ClientProxyTests::keyRepeat()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyRepeat(kKey, kMask, kCount, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyUp_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKUP" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
}

void ClientProxyTests::keyUp()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyUp(kKey, kMask, kButton);
  QCOMPARE(test.stream->take(), expected);
}

// Keyboard follow mode (protocol 1.9) tells a secondary whether it currently
// holds the keyboard and whether the mode is on at all (that second byte is what
// keeps its own cursor usable).  Clients older than 1.9 must never see it.
void ClientProxyTests::keyboardFollow_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<bool>("isTarget");
  QTest::addColumn<bool>("followMode");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.9 target") << 9 << true << true << "DKBF" + QByteArray::fromHex("0101");
  QTest::newRow("1.9 released") << 9 << false << true << "DKBF" + QByteArray::fromHex("0001");
  QTest::newRow("1.9 mode off") << 9 << true << false << "DKBF" + QByteArray::fromHex("0100");
  QTest::newRow("1.8 unaffected") << 8 << true << true << QByteArray();
  QTest::newRow("1.7 unaffected") << 7 << true << true << QByteArray();
}

void ClientProxyTests::keyboardFollow()
{
  QFETCH(int, minor);
  QFETCH(bool, isTarget);
  QFETCH(bool, followMode);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyboardFollow(isTarget, followMode);
  QCOMPARE(test.stream->take(), expected);
}

// CKBF asks for the keyboard.  It has to reach the server as an event, and its
// argument has to be consumed so the stream stays in sync with the client.
void ClientProxyTests::keyboardFollowRequest_data()
{
  QTest::addColumn<QByteArray>("request");
  QTest::addColumn<bool>("accepted");

  QTest::newRow("request") << "CKBF" + QByteArray::fromHex("00000007") << true;
  QTest::newRow("sequence zero") << "CKBF" + QByteArray::fromHex("00000000") << true;
}

void ClientProxyTests::keyboardFollowRequest()
{
  QFETCH(QByteArray, request);
  QFETCH(bool, accepted);

  FollowProxyUnderTest test;
  test.stream->feed(request);

  const bool result = test.asFollowProxy()->parseMessage(reinterpret_cast<const uint8_t *>(request.constData()));
  QCOMPARE(result, accepted);
  QCOMPARE(int(test.events.count(EventTypes::ServerKeyboardFollowRequested)), accepted ? 1 : 0);
}

QTEST_MAIN(ClientProxyTests)
