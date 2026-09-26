// SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
// SPDX-License-Identifier: MIT
#pragma once

#include <QTemporaryDir>
#include <QTranslator>
#include <QtTest>

class InputModeTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void init();
  void saveAndRestore();
  void cancelAndReset();
  void keyboardStatusAndIcons();
  void serviceMismatchStopsStarting();
  void bundledCoreRecoveryStopsServiceAndCanBeCancelled();
  void mainWindowModeAndTray();

private:
  QTemporaryDir m_settings;
  QTranslator m_translator;
};
