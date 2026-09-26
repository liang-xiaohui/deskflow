/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXKeyboardFollow.h"

#include <QtTest>

class OSXKeyboardFollowTests : public QObject
{
  Q_OBJECT

private:
  static CGEventRef key(CGKeyCode code, CGEventType type, CGEventFlags flags = 0)
  {
    auto event = CGEventCreateKeyboardEvent(nullptr, code, type == kCGEventKeyDown);
    CGEventSetType(event, type);
    CGEventSetFlags(event, flags);
    return event;
  }

private Q_SLOTS:
  void mouseOnlyTapCannotCaptureKeyboard()
  {
    // Actual mask returned by Quartz for a build with stale keyboard access.
    QVERIFY(!OSXKeyboardFollowState::canCaptureKeyboard(0xfffbf3ff));
    QVERIFY(OSXKeyboardFollowState::canCaptureKeyboard(kCGEventMaskForAllEvents));
    for (const auto event : {kCGEventKeyDown, kCGEventKeyUp, kCGEventFlagsChanged}) {
      QVERIFY(!OSXKeyboardFollowState::canCaptureKeyboard(kCGEventMaskForAllEvents & ~CGEventMaskBit(event)));
    }
  }

  void compensationDoesNotReleasePhysicalShift()
  {
    OSXKeyboardFollowState state;
    auto down = key(kVK_Shift, kCGEventFlagsChanged, kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK);
    state.update(down);
    CFRelease(down);

    auto compensation = key(kVK_Shift, kCGEventFlagsChanged);
    CGEventSetIntegerValueField(compensation, kCGEventSourceUserData, OSXKeyboardFollowState::localKeyMarker);
    state.update(compensation);
    QVERIFY(state.keys().test(kVK_Shift));
    QVERIFY(state.modifierFlags() & kCGEventFlagMaskShift);

    // Its identity remains present when Quartz delivers a copy later.
    auto copy = CGEventCreateCopy(compensation);
    QVERIFY(OSXKeyboardFollowState::isLocalKey(copy));
    CFRelease(copy);
    CFRelease(compensation);
  }

  void forwardedKeyKeepsPhysicalModifierAfterCompensation()
  {
    OSXKeyboardFollowState state;
    OSXKeyboardFollowState::Keys held;
    held.set(kVK_Shift);
    state.seed(held, 0);

    // OS state can lack Shift after the primary released it locally.
    auto a = key(kVK_ANSI_A, kCGEventKeyDown, kCGEventFlagMaskNumericPad);
    const auto flags = state.update(a);
    QVERIFY(flags & kCGEventFlagMaskShift);
    QVERIFY(flags & NX_DEVICELSHIFTKEYMASK);
    QVERIFY(flags & kCGEventFlagMaskNumericPad);
    QVERIFY(state.keys().test(kVK_ANSI_A));
    CFRelease(a);

    auto shiftUp = key(kVK_Shift, kCGEventFlagsChanged);
    QVERIFY(!(state.update(shiftUp) & kCGEventFlagMaskShift));
    CFRelease(shiftUp);
  }

  void releasingOneShiftKeepsTheOtherHeld()
  {
    OSXKeyboardFollowState state;
    auto left = key(kVK_Shift, kCGEventFlagsChanged, kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK);
    state.update(left);
    auto right =
        key(kVK_RightShift, kCGEventFlagsChanged,
            kCGEventFlagMaskShift | NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK);
    state.update(right);
    auto leftUp = key(kVK_Shift, kCGEventFlagsChanged, kCGEventFlagMaskShift | NX_DEVICERSHIFTKEYMASK);
    const auto flags = state.update(leftUp);
    QVERIFY(!state.keys().test(kVK_Shift));
    QVERIFY(state.keys().test(kVK_RightShift));
    QVERIFY(flags & kCGEventFlagMaskShift);
    QVERIFY(!(flags & NX_DEVICELSHIFTKEYMASK));
    CFRelease(left);
    CFRelease(right);
    CFRelease(leftUp);
  }

  void allModifiersTrackPressAndRelease()
  {
    for (const auto &modifier : OSXKeyboardFollowState::modifiers) {
      OSXKeyboardFollowState state;
      auto down = key(modifier.key, kCGEventFlagsChanged, modifier.deviceFlag | modifier.flag);
      QVERIFY(state.update(down) & modifier.flag);
      QVERIFY(state.keys().test(modifier.key));
      auto up = key(modifier.key, kCGEventFlagsChanged);
      QVERIFY(!(state.update(up) & modifier.flag));
      QVERIFY(!state.keys().test(modifier.key));
      CFRelease(down);
      CFRelease(up);
    }
  }

  void compensationDoesNotClearCapsLock()
  {
    OSXKeyboardFollowState state;
    state.seed({}, kCGEventFlagMaskAlphaShift);
    auto event = key(kVK_ANSI_A, kCGEventKeyUp);
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, OSXKeyboardFollowState::localKeyMarker);
    state.update(event);
    QVERIFY(state.lockFlags() & kCGEventFlagMaskAlphaShift);
    CFRelease(event);
  }

  void invalidKeyDoesNotChangeHeldKeys()
  {
    OSXKeyboardFollowState state;
    OSXKeyboardFollowState::Keys held;
    held.set(kVK_Command);
    state.seed(held, 0);
    auto event = key(65535, kCGEventKeyDown);
    state.update(event);
    QCOMPARE(state.keys(), held);
    CFRelease(event);
    QVERIFY(!OSXKeyboardFollowState::isLocalKey(nullptr));
  }
};

QTEST_APPLESS_MAIN(OSXKeyboardFollowTests)
#include "OSXKeyboardFollowTests.moc"
