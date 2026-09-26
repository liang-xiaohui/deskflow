/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "server/ClientProxy1_8.h"

class Server;
class IEventQueue;

//! Proxy for client implementing protocol version 1.9
/*!
Adds keyboard follow mode (server/keyboardFollow): the client can ask for the
keyboard when its own physical mouse moves, and the server tells it whether it
currently holds the keyboard.
*/
class ClientProxy1_9 : public ClientProxy1_8
{
public:
  ClientProxy1_9(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ClientProxy1_9(ClientProxy1_9 const &) = delete;
  ClientProxy1_9(ClientProxy1_9 &&) = delete;
  ~ClientProxy1_9() override = default;

  ClientProxy1_9 &operator=(ClientProxy1_9 const &) = delete;
  ClientProxy1_9 &operator=(ClientProxy1_9 &&) = delete;

  void keyboardFollow(bool isTarget) override;
  bool parseMessage(const uint8_t *code) override;
  bool keyboardFollowRequested();

private:
  bool m_isKeyboardFollowTarget = false;

  // ClientProxy1_6 keeps its event queue private, so keep our own reference
  IEventQueue *m_eventQueue = nullptr;
};
