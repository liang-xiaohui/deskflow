/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "client/ServerProxy1_8.h"

//! Proxy for server implementing protocol version 1.9
/*!
Adds keyboard follow mode: the client asks for the keyboard when its own
physical mouse moves, and the server reports who currently holds it.
*/
class ServerProxy1_9 : public ServerProxy1_8
{
public:
  ServerProxy1_9(Client *client, deskflow::IStream *stream, IEventQueue *events);
  ~ServerProxy1_9() override = default;

  void onLocalMouseActivity(uint32_t sequence) override;

protected:
  ConnectionResult parseMessage(const uint8_t *code) override;
};
