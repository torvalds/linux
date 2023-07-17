#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2018 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2018 Red Hat, Inc.
#

from . import base
import hidtools.hid
import libevdev
import logging

logger = logging.getLogger("hidtools.test.keyboard")


class InvalidHIDCommunication(Exception):
    pass


class KeyboardData(object):
    pass


class BaseKeyboard(base.UHIDTestDevice):
    def __init__(self, rdesc, name=None, input_info=None):
        assert rdesc is not None
        super().__init__(name, "Key", input_info=input_info, rdesc=rdesc)
        self.keystates = {}

    def _update_key_state(self, keys):
        """
        Update the internal state of keys with the new state given.

        :param key: a tuple of chars for the currently pressed keys.
        """
        # First remove the already released keys
        unused_keys = [k for k, v in self.keystates.items() if not v]
        for key in unused_keys:
            del self.keystates[key]

        # self.keystates contains now the list of currently pressed keys,
        # release them...
        for key in self.keystates.keys():
            self.keystates[key] = False

        # ...and press those that are in parameter
        for key in keys:
            self.keystates[key] = True

    def _create_report_data(self):
        keyboard = KeyboardData()
        for key, value in self.keystates.items():
            key = key.replace(" ", "").lower()
            setattr(keyboard, key, value)
        return keyboard

    def create_array_report(self, keys, reportID=None, application=None):
        """
        Return an input report for this device.

        :param keys: a tuple of chars for the pressed keys. The class maintains
            the list of currently pressed keys, so to release a key, the caller
            needs to call again this function without the key in this tuple.
        :param reportID: the numeric report ID for this report, if needed
        """
        self._update_key_state(keys)
        reportID = reportID or self.default_reportID

        keyboard = self._create_report_data()
        return self.create_report(keyboard, reportID=reportID, application=application)

    def event(self, keys, reportID=None, application=None):
        """
        Send an input event on the default report ID.

        :param keys: a tuple of chars for the pressed keys. The class maintains
            the list of currently pressed keys, so to release a key, the caller
            needs to call again this function without the key in this tuple.
        """
        r = self.create_array_report(keys, reportID, application)
        self.call_input_event(r)
        return [r]


class PlainKeyboard(BaseKeyboard):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,                    # Usage Page (Generic Desktop)
        0x09, 0x06,                    # Usage (Keyboard)
        0xa1, 0x01,                    # Collection (Application)
        0x85, 0x01,                    # .Report ID (1)
        0x05, 0x07,                    # .Usage Page (Keyboard)
        0x19, 0xe0,                    # .Usage Minimum (224)
        0x29, 0xe7,                    # .Usage Maximum (231)
        0x15, 0x00,                    # .Logical Minimum (0)
        0x25, 0x01,                    # .Logical Maximum (1)
        0x75, 0x01,                    # .Report Size (1)
        0x95, 0x08,                    # .Report Count (8)
        0x81, 0x02,                    # .Input (Data,Var,Abs)
        0x19, 0x00,                    # .Usage Minimum (0)
        0x29, 0x97,                    # .Usage Maximum (151)
        0x15, 0x00,                    # .Logical Minimum (0)
        0x25, 0x01,                    # .Logical Maximum (1)
        0x75, 0x01,                    # .Report Size (1)
        0x95, 0x98,                    # .Report Count (152)
        0x81, 0x02,                    # .Input (Data,Var,Abs)
        0xc0,                          # End Collection
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, input_info=None):
        super().__init__(rdesc, name, input_info)
        self.default_reportID = 1


class ArrayKeyboard(BaseKeyboard):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,                    # Usage Page (Generic Desktop)
        0x09, 0x06,                    # Usage (Keyboard)
        0xa1, 0x01,                    # Collection (Application)
        0x05, 0x07,                    # .Usage Page (Keyboard)
        0x19, 0xe0,                    # .Usage Minimum (224)
        0x29, 0xe7,                    # .Usage Maximum (231)
        0x15, 0x00,                    # .Logical Minimum (0)
        0x25, 0x01,                    # .Logical Maximum (1)
        0x75, 0x01,                    # .Report Size (1)
        0x95, 0x08,                    # .Report Count (8)
        0x81, 0x02,                    # .Input (Data,Var,Abs)
        0x95, 0x06,                    # .Report Count (6)
        0x75, 0x08,                    # .Report Size (8)
        0x15, 0x00,                    # .Logical Minimum (0)
        0x26, 0xa4, 0x00,              # .Logical Maximum (164)
        0x05, 0x07,                    # .Usage Page (Keyboard)
        0x19, 0x00,                    # .Usage Minimum (0)
        0x29, 0xa4,                    # .Usage Maximum (164)
        0x81, 0x00,                    # .Input (Data,Arr,Abs)
        0xc0,                          # End Collection
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, input_info=None):
        super().__init__(rdesc, name, input_info)

    def _create_report_data(self):
        data = KeyboardData()
        array = []

        hut = hidtools.hut.HUT

        # strip modifiers from the array
        for k, v in self.keystates.items():
            # we ignore depressed keys
            if not v:
                continue

            usage = hut[0x07].from_name[k].usage
            if usage >= 224 and usage <= 231:
                # modifier
                setattr(data, k.lower(), 1)
            else:
                array.append(k)

        # if array length is bigger than 6, report ErrorRollOver
        if len(array) > 6:
            array = ["ErrorRollOver"] * 6

        data.keyboard = array
        return data


class LEDKeyboard(ArrayKeyboard):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,                    # Usage Page (Generic Desktop)
        0x09, 0x06,                    # Usage (Keyboard)
        0xa1, 0x01,                    # Collection (Application)
        0x05, 0x07,                    # .Usage Page (Keyboard)
        0x19, 0xe0,                    # .Usage Minimum (224)
        0x29, 0xe7,                    # .Usage Maximum (231)
        0x15, 0x00,                    # .Logical Minimum (0)
        0x25, 0x01,                    # .Logical Maximum (1)
        0x75, 0x01,                    # .Report Size (1)
        0x95, 0x08,                    # .Report Count (8)
        0x81, 0x02,                    # .Input (Data,Var,Abs)
        0x95, 0x01,                    # .Report Count (1)
        0x75, 0x08,                    # .Report Size (8)
        0x81, 0x01,                    # .Input (Cnst,Arr,Abs)
        0x95, 0x05,                    # .Report Count (5)
        0x75, 0x01,                    # .Report Size (1)
        0x05, 0x08,                    # .Usage Page (LEDs)
        0x19, 0x01,                    # .Usage Minimum (1)
        0x29, 0x05,                    # .Usage Maximum (5)
        0x91, 0x02,                    # .Output (Data,Var,Abs)
        0x95, 0x01,                    # .Report Count (1)
        0x75, 0x03,                    # .Report Size (3)
        0x91, 0x01,                    # .Output (Cnst,Arr,Abs)
        0x95, 0x06,                    # .Report Count (6)
        0x75, 0x08,                    # .Report Size (8)
        0x15, 0x00,                    # .Logical Minimum (0)
        0x26, 0xa4, 0x00,              # .Logical Maximum (164)
        0x05, 0x07,                    # .Usage Page (Keyboard)
        0x19, 0x00,                    # .Usage Minimum (0)
        0x29, 0xa4,                    # .Usage Maximum (164)
        0x81, 0x00,                    # .Input (Data,Arr,Abs)
        0xc0,                          # End Collection
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, input_info=None):
        super().__init__(rdesc, name, input_info)


# Some Primax manufactured keyboards set the Usage Page after having defined
# some local Usages. It relies on the fact that the specification states that
# Usages are to be concatenated with Usage Pages upon finding a Main item (see
# 6.2.2.8). This test covers this case.
class PrimaxKeyboard(ArrayKeyboard):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,                    # Usage Page (Generic Desktop)
        0x09, 0x06,                    # Usage (Keyboard)
        0xA1, 0x01,                    # Collection (Application)
        0x05, 0x07,                    # .Usage Page (Keyboard)
        0x19, 0xE0,                    # .Usage Minimum (224)
        0x29, 0xE7,                    # .Usage Maximum (231)
        0x15, 0x00,                    # .Logical Minimum (0)
        0x25, 0x01,                    # .Logical Maximum (1)
        0x75, 0x01,                    # .Report Size (1)
        0x95, 0x08,                    # .Report Count (8)
        0x81, 0x02,                    # .Input (Data,Var,Abs)
        0x75, 0x08,                    # .Report Size (8)
        0x95, 0x01,                    # .Report Count (1)
        0x81, 0x01,                    # .Input (Data,Var,Abs)
        0x05, 0x08,                    # .Usage Page (LEDs)
        0x19, 0x01,                    # .Usage Minimum (1)
        0x29, 0x03,                    # .Usage Maximum (3)
        0x75, 0x01,                    # .Report Size (1)
        0x95, 0x03,                    # .Report Count (3)
        0x91, 0x02,                    # .Output (Data,Var,Abs)
        0x95, 0x01,                    # .Report Count (1)
        0x75, 0x05,                    # .Report Size (5)
        0x91, 0x01,                    # .Output (Constant)
        0x15, 0x00,                    # .Logical Minimum (0)
        0x26, 0xFF, 0x00,              # .Logical Maximum (255)
        0x19, 0x00,                    # .Usage Minimum (0)
        0x2A, 0xFF, 0x00,              # .Usage Maximum (255)
        0x05, 0x07,                    # .Usage Page (Keyboard)
        0x75, 0x08,                    # .Report Size (8)
        0x95, 0x06,                    # .Report Count (6)
        0x81, 0x00,                    # .Input (Data,Arr,Abs)
        0xC0,                          # End Collection
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, input_info=None):
        super().__init__(rdesc, name, input_info)


class BaseTest:
    class TestKeyboard(base.BaseTestCase.TestUhid):
        def test_single_key(self):
            """check for key reliability."""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()
            syn_event = self.syn_event

            r = uhdev.event(["a and A"])
            expected = [syn_event]
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_A, 1))
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn(expected, events)
            assert evdev.value[libevdev.EV_KEY.KEY_A] == 1

            r = uhdev.event([])
            expected = [syn_event]
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_A, 0))
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn(expected, events)
            assert evdev.value[libevdev.EV_KEY.KEY_A] == 0

        def test_two_keys(self):
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()
            syn_event = self.syn_event

            r = uhdev.event(["a and A", "q and Q"])
            expected = [syn_event]
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_A, 1))
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_Q, 1))
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn(expected, events)
            assert evdev.value[libevdev.EV_KEY.KEY_A] == 1

            r = uhdev.event([])
            expected = [syn_event]
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_A, 0))
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_Q, 0))
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn(expected, events)
            assert evdev.value[libevdev.EV_KEY.KEY_A] == 0
            assert evdev.value[libevdev.EV_KEY.KEY_Q] == 0

            r = uhdev.event(["c and C"])
            expected = [syn_event]
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_C, 1))
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn(expected, events)
            assert evdev.value[libevdev.EV_KEY.KEY_C] == 1

            r = uhdev.event(["c and C", "Spacebar"])
            expected = [syn_event]
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_SPACE, 1))
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_KEY.KEY_C) not in events
            self.assertInputEventsIn(expected, events)
            assert evdev.value[libevdev.EV_KEY.KEY_C] == 1
            assert evdev.value[libevdev.EV_KEY.KEY_SPACE] == 1

            r = uhdev.event(["Spacebar"])
            expected = [syn_event]
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_C, 0))
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_KEY.KEY_SPACE) not in events
            self.assertInputEventsIn(expected, events)
            assert evdev.value[libevdev.EV_KEY.KEY_C] == 0
            assert evdev.value[libevdev.EV_KEY.KEY_SPACE] == 1

            r = uhdev.event([])
            expected = [syn_event]
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_SPACE, 0))
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn(expected, events)
            assert evdev.value[libevdev.EV_KEY.KEY_SPACE] == 0

        def test_modifiers(self):
            # ctrl-alt-del would be very nice :)
            uhdev = self.uhdev
            syn_event = self.syn_event

            r = uhdev.event(["LeftControl", "LeftShift", "= and +"])
            expected = [syn_event]
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_LEFTCTRL, 1))
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_LEFTSHIFT, 1))
            expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_EQUAL, 1))
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn(expected, events)


class TestPlainKeyboard(BaseTest.TestKeyboard):
    def create_device(self):
        return PlainKeyboard()

    def test_10_keys(self):
        uhdev = self.uhdev
        syn_event = self.syn_event

        r = uhdev.event(
            [
                "1 and !",
                "2 and @",
                "3 and #",
                "4 and $",
                "5 and %",
                "6 and ^",
                "7 and &",
                "8 and *",
                "9 and (",
                "0 and )",
            ]
        )
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_0, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_1, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_2, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_3, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_4, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_5, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_6, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_7, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_8, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_9, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)

        r = uhdev.event([])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_0, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_1, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_2, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_3, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_4, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_5, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_6, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_7, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_8, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_9, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)


class TestArrayKeyboard(BaseTest.TestKeyboard):
    def create_device(self):
        return ArrayKeyboard()

    def test_10_keys(self):
        uhdev = self.uhdev
        syn_event = self.syn_event

        r = uhdev.event(
            [
                "1 and !",
                "2 and @",
                "3 and #",
                "4 and $",
                "5 and %",
                "6 and ^",
            ]
        )
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_1, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_2, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_3, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_4, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_5, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_6, 1))
        events = uhdev.next_sync_events()

        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)

        # ErrRollOver
        r = uhdev.event(
            [
                "1 and !",
                "2 and @",
                "3 and #",
                "4 and $",
                "5 and %",
                "6 and ^",
                "7 and &",
                "8 and *",
                "9 and (",
                "0 and )",
            ]
        )
        events = uhdev.next_sync_events()

        self.debug_reports(r, uhdev, events)

        assert len(events) == 0

        r = uhdev.event([])
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_1, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_2, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_3, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_4, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_5, 0))
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_6, 0))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEventsIn(expected, events)


class TestLEDKeyboard(BaseTest.TestKeyboard):
    def create_device(self):
        return LEDKeyboard()


class TestPrimaxKeyboard(BaseTest.TestKeyboard):
    def create_device(self):
        return PrimaxKeyboard()
