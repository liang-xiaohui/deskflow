/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_9.h"

#include "base/Event.h"
#include "base/Log.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"
#include "server/Server.h"

#include <cstring>

//
// ClientProxy1_9
//

ClientProxy1_9::ClientProxy1_9(
    const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events
)
    : ClientProxy1_8(name, stream, server, events),
      m_eventQueue(events)
{
  // do nothing
}

void ClientProxy1_9::keyboardFollow(bool isTarget, bool followMode)
{
  if (isTarget == m_isKeyboardFollowTarget && followMode == m_keyboardFollowMode) {
    return;
  }
  m_isKeyboardFollowTarget = isTarget;
  m_keyboardFollowMode = followMode;

  LOG_VERBOSE(
      "send keyboard follow state (target=%d mode=%d) to \"%s\"", isTarget ? 1 : 0, followMode ? 1 : 0, getName().c_str()
  );
  ProtocolUtil::writef(getStream(), kMsgDKeyboardFollow, isTarget ? 1 : 0, followMode ? 1 : 0);
}

bool ClientProxy1_9::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgCKeyboardFollow, 4) == 0) {
    return keyboardFollowRequested();
  }

  return ClientProxy1_8::parseMessage(code);
}

bool ClientProxy1_9::keyboardFollowRequested()
{
  uint32_t sequence = 0;
  if (!ProtocolUtil::readf(getStream(), kMsgCKeyboardFollow + 4, &sequence)) {
    return false;
  }

  LOG_VERBOSE("recv keyboard follow request (sequence=%u) from \"%s\"", sequence, getName().c_str());
  m_eventQueue->addEvent(Event(EventTypes::ServerKeyboardFollowRequested, getEventTarget()));
  return true;
}
