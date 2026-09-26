/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "arch/Arch.h"
#include "base/Log.h"
#include <QTemporaryDir>
#include <QTest>

class ClientTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void followsOnlyWhenEnabled();
  void doesNotReclaimAfterOwnershipLoss();
  void cooldownExpiresWhileStationary();
  void cleanupReleasesKeysAndTimer_data();
  void cleanupReleasesKeysAndTimer();
  void reconnectToOlderServer();
  void protocolStateBeforeAndAfterHandshake();

private:
  Arch m_arch;
  Log m_log;
  QTemporaryDir m_settings;
};
