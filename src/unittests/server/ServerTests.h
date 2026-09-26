/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "arch/Arch.h"
#include "base/Log.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include <QTemporaryDir>
#include <QTest>

class ServerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void mouseFollowRoutesOnlyTheKeyboard();
  void heldModifiersMoveAndDisconnectRestoresLocal();
  void guiReconnectReceivesCurrentKeyboardTarget();
#ifdef WIN32
  void workerCancellationCompletes();
#endif
  void SwitchToScreenInfo_alloc_screen();
  void KeyboardBroadcastInfo_alloc_stateAndSceens();

private:
  Arch m_arch;
  Log m_log;
  QTemporaryDir m_settings;
  std::unique_ptr<deskflow::core::ipc::CoreIpcServer> m_ipc;
};
