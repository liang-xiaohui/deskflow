/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/IPlatformScreen.h"

// An in-memory screen: tests never install hooks or inject system input.
class FakePlatformScreen : public IPlatformScreen
{
public:
  explicit FakePlatformScreen(IEventQueue *events, bool primary = false) : IPlatformScreen(events), primary(primary)
  {
  }
  bool primary = false;
  bool supported = true;
  bool positionAvailable = true;
  bool localCursor = false;
  bool diverted = false;
  int releases = 0;
  int warps = 0;
  int32_t x = 100;
  int32_t y = 100;

  void *getEventTarget() const override
  {
    return const_cast<FakePlatformScreen *>(this);
  }
  bool getClipboard(ClipboardID, IClipboard *) const override
  {
    return false;
  }
  void getShape(int32_t &sx, int32_t &sy, int32_t &w, int32_t &h) const override
  {
    sx = sy = 0;
    w = 1920;
    h = 1080;
  }
  void getCursorPos(int32_t &cx, int32_t &cy) const override
  {
    cx = x;
    cy = y;
  }
  bool getLocalCursorPos(int32_t &cx, int32_t &cy) const override
  {
    getCursorPos(cx, cy);
    return positionAvailable;
  }
  void enable() override
  {
  }
  void disable() override
  {
  }
  void enter() override
  {
  }
  bool canLeave() override
  {
    return true;
  }
  void leave() override
  {
  }
  bool setClipboard(ClipboardID, const IClipboard *) override
  {
    return true;
  }
  void checkClipboards() override
  {
  }
  void openScreensaver(bool) override
  {
  }
  void closeScreensaver() override
  {
  }
  void screensaver(bool) override
  {
  }
  void resetOptions() override
  {
  }
  void setOptions(const OptionsList &) override
  {
  }
  void setSequenceNumber(uint32_t) override
  {
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }
  bool isPrimary() const override
  {
    return primary;
  }
  bool supportsKeyboardFollow() const override
  {
    return supported;
  }
  void setKeyboardFollowLocalCursor(bool value) override
  {
    localCursor = value;
  }
  void setKeyboardFollowDivert(bool value) override
  {
    diverted = value;
  }
  void reconfigure(uint32_t) override
  {
  }
  uint32_t activeSides() override
  {
    return 0;
  }
  void warpCursor(int32_t, int32_t) override
  {
    ++warps;
  }
  uint32_t registerHotKey(KeyID, KeyModifierMask) override
  {
    return 1;
  }
  void unregisterHotKey(uint32_t) override
  {
  }
  void fakeInputBegin() override
  {
  }
  void fakeInputEnd() override
  {
  }
  int32_t getJumpZoneSize() const override
  {
    return 0;
  }
  bool isAnyMouseButtonDown(uint32_t &) const override
  {
    return false;
  }
  void getCursorCenter(int32_t &cx, int32_t &cy) const override
  {
    cx = 960;
    cy = 540;
  }
  void fakeMouseButton(ButtonID, bool) override
  {
  }
  void fakeMouseMove(int32_t, int32_t) override
  {
    ++warps;
  }
  void fakeMouseRelativeMove(int32_t, int32_t) const override
  {
  }
  void fakeMouseWheel(ScrollDelta) const override
  {
  }
  void updateKeyMap() override
  {
  }
  void updateKeyState() override
  {
  }
  void setHalfDuplexMask(KeyModifierMask) override
  {
  }
  void fakeKeyDown(KeyID, KeyModifierMask, KeyButton, const std::string &) override
  {
  }
  bool fakeKeyRepeat(KeyID, KeyModifierMask, int32_t, KeyButton, const std::string &) override
  {
    return true;
  }
  bool fakeKeyUp(KeyButton) override
  {
    return true;
  }
  void fakeAllKeysUp() override
  {
    ++releases;
  }
  bool fakeCtrlAltDel() override
  {
    return false;
  }
  bool isKeyDown(KeyButton) const override
  {
    return false;
  }
  KeyModifierMask getActiveModifiers() const override
  {
    return 0;
  }
  KeyModifierMask pollActiveModifiers() const override
  {
    return 0;
  }
  int32_t pollActiveGroup() const override
  {
    return 0;
  }
  void pollPressedKeys(KeyButtonSet &) const override
  {
  }

protected:
  void handleSystemEvent(const Event &) override
  {
  }
};
