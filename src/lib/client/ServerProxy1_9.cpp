/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxy1_9.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <cstring>

ServerProxy1_9::ServerProxy1_9(Client *client, deskflow::IStream *stream, IEventQueue *events)
    : ServerProxy1_8(client, stream, events)
{
}

void ServerProxy1_9::onLocalMouseActivity(uint32_t sequence)
{
  LOG_VERBOSE("send keyboard follow request (sequence=%u)", sequence);
  ProtocolUtil::writef(getStream(), kMsgCKeyboardFollow, sequence);
}

ServerProxy::ConnectionResult ServerProxy1_9::parseHandshakeMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDKeyboardFollow, 4) == 0) {
    // the server announces the keyboard follow mode as soon as it accepts us,
    // which can happen before our handshake has completed
    return keyboardFollowState() ? ConnectionResult::Okay : ConnectionResult::Disconnect;
  }

  return ServerProxy1_8::parseHandshakeMessage(code);
}

ServerProxy::ConnectionResult ServerProxy1_9::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDKeyboardFollow, 4) == 0) {
    return keyboardFollowState() ? ConnectionResult::Okay : ConnectionResult::Disconnect;
  }

  return ServerProxy1_8::parseMessage(code);
}

bool ServerProxy1_9::keyboardFollowState()
{
  int8_t isTarget = 0;
  int8_t followMode = 0;
  if (!ProtocolUtil::readf(getStream(), kMsgDKeyboardFollow + 4, &isTarget, &followMode)) {
    return false;
  }
  LOG_VERBOSE("recv keyboard follow state (target=%d mode=%d)", isTarget, followMode);

  keyboardFollowChanged(isTarget != 0, followMode != 0);
  return true;
}
