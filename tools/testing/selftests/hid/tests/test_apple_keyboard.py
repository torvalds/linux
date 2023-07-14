#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2019 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2019 Red Hat, Inc.
#

from .test_keyboard import ArrayKeyboard, TestArrayKeyboard
from hidtools.util import BusType

import libevdev
import logging

logger = logging.getLogger("hidtools.test.apple-keyboard")

KERNEL_MODULE = ("apple", "hid-apple")


class KbdData(object):
    pass


class AppleKeyboard(ArrayKeyboard):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,         # Usage Page (Generic Desktop)
        0x09, 0x06,         # Usage (Keyboard)
        0xa1, 0x01,         # Collection (Application)
        0x85, 0x01,         # .Report ID (1)
        0x05, 0x07,         # .Usage Page (Keyboard)
        0x19, 0xe0,         # .Usage Minimum (224)
        0x29, 0xe7,         # .Usage Maximum (231)
        0x15, 0x00,         # .Logical Minimum (0)
        0x25, 0x01,         # .Logical Maximum (1)
        0x75, 0x01,         # .Report Size (1)
        0x95, 0x08,         # .Report Count (8)
        0x81, 0x02,         # .Input (Data,Var,Abs)
        0x75, 0x08,         # .Report Size (8)
        0x95, 0x01,         # .Report Count (1)
        0x81, 0x01,         # .Input (Cnst,Arr,Abs)
        0x75, 0x01,         # .Report Size (1)
        0x95, 0x05,         # .Report Count (5)
        0x05, 0x08,         # .Usage Page (LEDs)
        0x19, 0x01,         # .Usage Minimum (1)
        0x29, 0x05,         # .Usage Maximum (5)
        0x91, 0x02,         # .Output (Data,Var,Abs)
        0x75, 0x03,         # .Report Size (3)
        0x95, 0x01,         # .Report Count (1)
        0x91, 0x01,         # .Output (Cnst,Arr,Abs)
        0x75, 0x08,         # .Report Size (8)
        0x95, 0x06,         # .Report Count (6)
        0x15, 0x00,         # .Logical Minimum (0)
        0x26, 0xff, 0x00,   # .Logical Maximum (255)
        0x05, 0x07,         # .Usage Page (Keyboard)
        0x19, 0x00,         # .Usage Minimum (0)
        0x2a, 0xff, 0x00,   # .Usage Maximum (255)
        0x81, 0x00,         # .Input (Data,Arr,Abs)
        0xc0,               # End Collection
        0x05, 0x0c,         # Usage Page (Consumer Devices)
        0x09, 0x01,         # Usage (Consumer Control)
        0xa1, 0x01,         # Collection (Application)
        0x85, 0x47,         # .Report ID (71)
        0x05, 0x01,         # .Usage Page (Generic Desktop)
        0x09, 0x06,         # .Usage (Keyboard)
        0xa1, 0x02,         # .Collection (Logical)
        0x05, 0x06,         # ..Usage Page (Generic Device Controls)
        0x09, 0x20,         # ..Usage (Battery Strength)
        0x15, 0x00,         # ..Logical Minimum (0)
        0x26, 0xff, 0x00,   # ..Logical Maximum (255)
        0x75, 0x08,         # ..Report Size (8)
        0x95, 0x01,         # ..Report Count (1)
        0x81, 0x02,         # ..Input (Data,Var,Abs)
        0xc0,               # .End Collection
        0xc0,               # End Collection
        0x05, 0x0c,         # Usage Page (Consumer Devices)
        0x09, 0x01,         # Usage (Consumer Control)
        0xa1, 0x01,         # Collection (Application)
        0x85, 0x11,         # .Report ID (17)
        0x15, 0x00,         # .Logical Minimum (0)
        0x25, 0x01,         # .Logical Maximum (1)
        0x75, 0x01,         # .Report Size (1)
        0x95, 0x03,         # .Report Count (3)
        0x81, 0x01,         # .Input (Cnst,Arr,Abs)
        0x75, 0x01,         # .Report Size (1)
        0x95, 0x01,         # .Report Count (1)
        0x05, 0x0c,         # .Usage Page (Consumer Devices)
        0x09, 0xb8,         # .Usage (Eject)
        0x81, 0x02,         # .Input (Data,Var,Abs)
        0x06, 0xff, 0x00,   # .Usage Page (Vendor Usage Page 0xff)
        0x09, 0x03,         # .Usage (Vendor Usage 0x03)
        0x81, 0x02,         # .Input (Data,Var,Abs)
        0x75, 0x01,         # .Report Size (1)
        0x95, 0x03,         # .Report Count (3)
        0x81, 0x01,         # .Input (Cnst,Arr,Abs)
        0x05, 0x0c,         # .Usage Page (Consumer Devices)
        0x85, 0x12,         # .Report ID (18)
        0x15, 0x00,         # .Logical Minimum (0)
        0x25, 0x01,         # .Logical Maximum (1)
        0x75, 0x01,         # .Report Size (1)
        0x95, 0x01,         # .Report Count (1)
        0x09, 0xcd,         # .Usage (Play/Pause)
        0x81, 0x02,         # .Input (Data,Var,Abs)
        0x09, 0xb3,         # .Usage (Fast Forward)
        0x81, 0x02,         # .Input (Data,Var,Abs)
        0x09, 0xb4,         # .Usage (Rewind)
        0x81, 0x02,         # .Input (Data,Var,Abs)
        0x09, 0xb5,         # .Usage (Scan Next Track)
        0x81, 0x02,         # .Input (Data,Var,Abs)
        0x09, 0xb6,         # .Usage (Scan Previous Track)
        0x81, 0x02,         # .Input (Data,Var,Abs)
        0x81, 0x01,         # .Input (Cnst,Arr,Abs)
        0x81, 0x01,         # .Input (Cnst,Arr,Abs)
        0x81, 0x01,         # .Input (Cnst,Arr,Abs)
        0x85, 0x13,         # .Report ID (19)
        0x15, 0x00,         # .Logical Minimum (0)
        0x25, 0x01,         # .Logical Maximum (1)
        0x75, 0x01,         # .Report Size (1)
        0x95, 0x01,         # .Report Count (1)
        0x06, 0x01, 0xff,   # .Usage Page (Vendor Usage Page 0xff01)
        0x09, 0x0a,         # .Usage (Vendor Usage 0x0a)
        0x81, 0x02,         # .Input (Data,Var,Abs)
        0x06, 0x01, 0xff,   # .Usage Page (Vendor Usage Page 0xff01)
        0x09, 0x0c,         # .Usage (Vendor Usage 0x0c)
        0x81, 0x22,         # .Input (Data,Var,Abs,NoPref)
        0x75, 0x01,         # .Report Size (1)
        0x95, 0x06,         # .Report Count (6)
        0x81, 0x01,         # .Input (Cnst,Arr,Abs)
        0x85, 0x09,         # .Report ID (9)
        0x09, 0x0b,         # .Usage (Vendor Usage 0x0b)
        0x75, 0x08,         # .Report Size (8)
        0x95, 0x01,         # .Report Count (1)
        0xb1, 0x02,         # .Feature (Data,Var,Abs)
        0x75, 0x08,         # .Report Size (8)
        0x95, 0x02,         # .Report Count (2)
        0xb1, 0x01,         # .Feature (Cnst,Arr,Abs)
        0xc0,               # End Collection
    ]
    # fmt: on

    def __init__(
        self,
        rdesc=report_descriptor,
        name="Apple Wireless Keyboard",
        input_info=(BusType.BLUETOOTH, 0x05AC, 0x0256),
    ):
        super().__init__(rdesc, name, input_info)
        self.default_reportID = 1

    def send_fn_state(self, state):
        data = KbdData()
        setattr(data, "0xff0003", state)
        r = self.create_report(data, reportID=17)
        self.call_input_event(r)
        return [r]


class TestAppleKeyboard(TestArrayKeyboard):
    kernel_modules = [KERNEL_MODULE]

    def create_device(self):
        return AppleKeyboard()

    def test_single_function_key(self):
        """check for function key reliability."""
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()
        syn_event = self.syn_event

        r = uhdev.event(["F4"])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_ALL_APPLICATIONS, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_ALL_APPLICATIONS] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 0

        r = uhdev.event([])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_ALL_APPLICATIONS, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_ALL_APPLICATIONS] == 0

    def test_single_fn_function_key(self):
        """check for function key reliability with the fn key."""
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()
        syn_event = self.syn_event

        r = uhdev.send_fn_state(1)
        r.extend(uhdev.event(["F4"]))
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_F4, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 1

        r = uhdev.event([])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_F4, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 1

        r = uhdev.send_fn_state(0)
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)

    def test_single_fn_function_key_release_first(self):
        """check for function key reliability with the fn key."""
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()
        syn_event = self.syn_event

        r = uhdev.send_fn_state(1)
        r.extend(uhdev.event(["F4"]))
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_F4, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 1

        r = uhdev.send_fn_state(0)
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)

        r = uhdev.event([])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_F4, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 0

    def test_single_fn_function_key_inverted(self):
        """check for function key reliability with the fn key."""
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()
        syn_event = self.syn_event

        r = uhdev.event(["F4"])
        r.extend(uhdev.send_fn_state(1))
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_ALL_APPLICATIONS, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_ALL_APPLICATIONS] == 1

        r = uhdev.event([])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_ALL_APPLICATIONS, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_ALL_APPLICATIONS] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 1

        r = uhdev.send_fn_state(0)
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)

    def test_multiple_fn_function_key_release_first(self):
        """check for function key reliability with the fn key."""
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()
        syn_event = self.syn_event

        r = uhdev.send_fn_state(1)
        r.extend(uhdev.event(["F4"]))
        r.extend(uhdev.event(["F4", "F6"]))
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_F4, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_F6, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_F6] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 1

        r = uhdev.event(["F6"])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_F4, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_F6] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 1

        r = uhdev.send_fn_state(0)
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_F6] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 0

        r = uhdev.event([])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_F6, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_F6] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 0

    def test_multiple_fn_function_key_release_between(self):
        """check for function key reliability with the fn key."""
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()
        syn_event = self.syn_event

        # press F4
        r = uhdev.event(["F4"])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_ALL_APPLICATIONS, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_ALL_APPLICATIONS] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_F6] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_KBDILLUMUP] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 0

        # press Fn key
        r = uhdev.send_fn_state(1)
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_ALL_APPLICATIONS] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_F6] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_KBDILLUMUP] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 1

        # keep F4 and press F6
        r = uhdev.event(["F4", "F6"])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_F6, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_ALL_APPLICATIONS] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_F6] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_KBDILLUMUP] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 1

        # keep F4 and F6
        r = uhdev.event(["F4", "F6"])
        expected = []
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_ALL_APPLICATIONS] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_F6] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_KBDILLUMUP] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 1

        # release Fn key and all keys
        r = uhdev.send_fn_state(0)
        r.extend(uhdev.event([]))
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_ALL_APPLICATIONS, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_F6, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_F4] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_ALL_APPLICATIONS] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_F6] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_KBDILLUMUP] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 0

    def test_single_pageup_key_release_first(self):
        """check for function key reliability with the [page] up key."""
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()
        syn_event = self.syn_event

        r = uhdev.send_fn_state(1)
        r.extend(uhdev.event(["UpArrow"]))
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_PAGEUP, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_PAGEUP] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_UP] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 1

        r = uhdev.send_fn_state(0)
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_FN, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_PAGEUP] == 1
        assert evdev.value[libevdev.EV_KEY.KEY_UP] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 0

        r = uhdev.event([])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_PAGEUP, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)
        assert evdev.value[libevdev.EV_KEY.KEY_PAGEUP] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_UP] == 0
        assert evdev.value[libevdev.EV_KEY.KEY_FN] == 0
