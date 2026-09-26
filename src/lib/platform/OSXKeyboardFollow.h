/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <Carbon/Carbon.h>
#include <IOKit/hidsystem/IOLLEvent.h>

#include <bitset>

// Kept apart from OS key state: local compensation must not change which
// physical keys we restore or which modifiers we forward to the next target.
// OSXScreen serializes access between its event thread and Quartz tap thread.
class OSXKeyboardFollowState
{
public:
  static constexpr int64_t localKeyMarker = 0x44464b46;
  static constexpr CGEventMask requiredKeyboardEvents =
      CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) | CGEventMaskBit(kCGEventFlagsChanged);
  using Keys = std::bitset<128>;

  struct Modifier
  {
    CGKeyCode key;
    CGEventFlags deviceFlag;
    CGEventFlags flag;
  };

  inline static constexpr Modifier modifiers[] = {
      {kVK_Shift, NX_DEVICELSHIFTKEYMASK, kCGEventFlagMaskShift},
      {kVK_RightShift, NX_DEVICERSHIFTKEYMASK, kCGEventFlagMaskShift},
      {kVK_Control, NX_DEVICELCTLKEYMASK, kCGEventFlagMaskControl},
      {kVK_RightControl, NX_DEVICERCTLKEYMASK, kCGEventFlagMaskControl},
      {kVK_Option, NX_DEVICELALTKEYMASK, kCGEventFlagMaskAlternate},
      {kVK_RightOption, NX_DEVICERALTKEYMASK, kCGEventFlagMaskAlternate},
      {kVK_Command, NX_DEVICELCMDKEYMASK, kCGEventFlagMaskCommand},
      {kVK_RightCommand, NX_DEVICERCMDKEYMASK, kCGEventFlagMaskCommand},
      {kVK_Function, kCGEventFlagMaskSecondaryFn, kCGEventFlagMaskSecondaryFn},
  };

  void seed(const Keys &keys, CGEventFlags flags)
  {
    m_keys = keys;
    m_lockFlags = flags & kCGEventFlagMaskAlphaShift;
  }

  const Keys &keys() const
  {
    return m_keys;
  }

  CGEventFlags lockFlags() const
  {
    return m_lockFlags;
  }

  CGEventFlags modifierFlags() const
  {
    CGEventFlags flags = m_lockFlags;
    for (const auto &modifier : modifiers) {
      if (m_keys.test(modifier.key)) {
        flags |= modifier.deviceFlag | modifier.flag;
      }
    }
    return flags;
  }

  static bool isLocalKey(CGEventRef event)
  {
    return event != nullptr && CGEventGetIntegerValueField(event, kCGEventSourceUserData) == localKeyMarker;
  }

  static bool canCaptureKeyboard(CGEventMask mask)
  {
    return (mask & requiredKeyboardEvents) == requiredKeyboardEvents;
  }

  CGEventFlags update(CGEventRef event)
  {
    if (isLocalKey(event)) {
      return modifierFlags();
    }
    const auto key = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    const auto type = CGEventGetType(event);
    const auto flags = CGEventGetFlags(event);
    m_lockFlags = flags & kCGEventFlagMaskAlphaShift;
    if (key >= 0 && key < static_cast<int64_t>(m_keys.size())) {
      if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
        m_keys.set(key, type == kCGEventKeyDown);
      } else if (type == kCGEventFlagsChanged) {
        for (const auto &modifier : modifiers) {
          if (key == modifier.key) {
            m_keys.set(key, (flags & modifier.deviceFlag) != 0);
            break;
          }
        }
      }
    }

    CGEventFlags managed = kCGEventFlagMaskAlphaShift;
    for (const auto &modifier : modifiers) {
      managed |= modifier.deviceFlag | modifier.flag;
    }
    return (flags & ~managed) | modifierFlags();
  }

private:
  Keys m_keys;
  CGEventFlags m_lockFlags = 0;
};
