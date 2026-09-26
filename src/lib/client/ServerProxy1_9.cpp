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

ServerProxy::ConnectionResult ServerProxy1_9::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDKeyboardFollow, 4) == 0) {
    int8_t isTarget = 0;
    if (!ProtocolUtil::readf(getStream(), kMsgDKeyboardFollow + 4, &isTarget)) {
      return ConnectionResult::Disconnect;
    }
    LOG_VERBOSE("recv keyboard follow state %d", isTarget);

    keyboardFollowChanged(isTarget != 0);
    return ConnectionResult::Okay;
  }

  return ServerProxy1_8::parseMessage(code);
}
