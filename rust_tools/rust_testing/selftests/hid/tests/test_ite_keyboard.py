#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2020 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2020 Red Hat, Inc.
#

from .test_keyboard import ArrayKeyboard, TestArrayKeyboard
from hidtools.util import BusType

import libevdev
import logging
from . import base

logger = logging.getLogger("hidtools.test.ite-keyboard")

KERNEL_MODULE = base.KernelModule("itetech", "hid_ite")


class KbdData(object):
    pass


# The ITE keyboards have an issue regarding the Wifi key:
# nothing comes in when pressing the key, but we get a null
# event on the key release.
# This test covers this case.
class ITEKeyboard(ArrayKeyboard):
    # fmt: off
    report_descriptor = [
        0x06, 0x85, 0xff,              # Usage Page (Vendor Usage Page 0xff85)
        0x09, 0x95,                    # Usage (Vendor Usage 0x95)           3
        0xa1, 0x01,                    # Collection (Application)            5
        0x85, 0x5a,                    # .Report ID (90)                     7
        0x09, 0x01,                    # .Usage (Vendor Usage 0x01)          9
        0x15, 0x00,                    # .Logical Minimum (0)                11
        0x26, 0xff, 0x00,              # .Logical Maximum (255)              13
        0x75, 0x08,                    # .Report Size (8)                    16
        0x95, 0x10,                    # .Report Count (16)                  18
        0xb1, 0x00,                    # .Feature (Data,Arr,Abs)             20
        0xc0,                          # End Collection                      22
        0x05, 0x01,                    # Usage Page (Generic Desktop)        23
        0x09, 0x06,                    # Usage (Keyboard)                    25
        0xa1, 0x01,                    # Collection (Application)            27
        0x85, 0x01,                    # .Report ID (1)                      29
        0x75, 0x01,                    # .Report Size (1)                    31
        0x95, 0x08,                    # .Report Count (8)                   33
        0x05, 0x07,                    # .Usage Page (Keyboard)              35
        0x19, 0xe0,                    # .Usage Minimum (224)                37
        0x29, 0xe7,                    # .Usage Maximum (231)                39
        0x15, 0x00,                    # .Logical Minimum (0)                41
        0x25, 0x01,                    # .Logical Maximum (1)                43
        0x81, 0x02,                    # .Input (Data,Var,Abs)               45
        0x95, 0x01,                    # .Report Count (1)                   47
        0x75, 0x08,                    # .Report Size (8)                    49
        0x81, 0x03,                    # .Input (Cnst,Var,Abs)               51
        0x95, 0x05,                    # .Report Count (5)                   53
        0x75, 0x01,                    # .Report Size (1)                    55
        0x05, 0x08,                    # .Usage Page (LEDs)                  57
        0x19, 0x01,                    # .Usage Minimum (1)                  59
        0x29, 0x05,                    # .Usage Maximum (5)                  61
        0x91, 0x02,                    # .Output (Data,Var,Abs)              63
        0x95, 0x01,                    # .Report Count (1)                   65
        0x75, 0x03,                    # .Report Size (3)                    67
        0x91, 0x03,                    # .Output (Cnst,Var,Abs)              69
        0x95, 0x06,                    # .Report Count (6)                   71
        0x75, 0x08,                    # .Report Size (8)                    73
        0x15, 0x00,                    # .Logical Minimum (0)                75
        0x26, 0xff, 0x00,              # .Logical Maximum (255)              77
        0x05, 0x07,                    # .Usage Page (Keyboard)              80
        0x19, 0x00,                    # .Usage Minimum (0)                  82
        0x2a, 0xff, 0x00,              # .Usage Maximum (255)                84
        0x81, 0x00,                    # .Input (Data,Arr,Abs)               87
        0xc0,                          # End Collection                      89
        0x05, 0x0c,                    # Usage Page (Consumer Devices)       90
        0x09, 0x01,                    # Usage (Consumer Control)            92
        0xa1, 0x01,                    # Collection (Application)            94
        0x85, 0x02,                    # .Report ID (2)                      96
        0x19, 0x00,                    # .Usage Minimum (0)                  98
        0x2a, 0x3c, 0x02,              # .Usage Maximum (572)                100
        0x15, 0x00,                    # .Logical Minimum (0)                103
        0x26, 0x3c, 0x02,              # .Logical Maximum (572)              105
        0x75, 0x10,                    # .Report Size (16)                   108
        0x95, 0x01,                    # .Report Count (1)                   110
        0x81, 0x00,                    # .Input (Data,Arr,Abs)               112
        0xc0,                          # End Collection                      114
        0x05, 0x01,                    # Usage Page (Generic Desktop)        115
        0x09, 0x0c,                    # Usage (Wireless Radio Controls)     117
        0xa1, 0x01,                    # Collection (Application)            119
        0x85, 0x03,                    # .Report ID (3)                      121
        0x15, 0x00,                    # .Logical Minimum (0)                123
        0x25, 0x01,                    # .Logical Maximum (1)                125
        0x09, 0xc6,                    # .Usage (Wireless Radio Button)      127
        0x95, 0x01,                    # .Report Count (1)                   129
        0x75, 0x01,                    # .Report Size (1)                    131
        0x81, 0x06,                    # .Input (Data,Var,Rel)               133
        0x75, 0x07,                    # .Report Size (7)                    135
        0x81, 0x03,                    # .Input (Cnst,Var,Abs)               137
        0xc0,                          # End Collection                      139
        0x05, 0x88,                    # Usage Page (Vendor Usage Page 0x88) 140
        0x09, 0x01,                    # Usage (Vendor Usage 0x01)           142
        0xa1, 0x01,                    # Collection (Application)            144
        0x85, 0x04,                    # .Report ID (4)                      146
        0x19, 0x00,                    # .Usage Minimum (0)                  148
        0x2a, 0xff, 0xff,              # .Usage Maximum (65535)              150
        0x15, 0x00,                    # .Logical Minimum (0)                153
        0x26, 0xff, 0xff,              # .Logical Maximum (65535)            155
        0x75, 0x08,                    # .Report Size (8)                    158
        0x95, 0x02,                    # .Report Count (2)                   160
        0x81, 0x02,                    # .Input (Data,Var,Abs)               162
        0xc0,                          # End Collection                      164
        0x05, 0x01,                    # Usage Page (Generic Desktop)        165
        0x09, 0x80,                    # Usage (System Control)              167
        0xa1, 0x01,                    # Collection (Application)            169
        0x85, 0x05,                    # .Report ID (5)                      171
        0x19, 0x81,                    # .Usage Minimum (129)                173
        0x29, 0x83,                    # .Usage Maximum (131)                175
        0x15, 0x00,                    # .Logical Minimum (0)                177
        0x25, 0x01,                    # .Logical Maximum (1)                179
        0x95, 0x08,                    # .Report Count (8)                   181
        0x75, 0x01,                    # .Report Size (1)                    183
        0x81, 0x02,                    # .Input (Data,Var,Abs)               185
        0xc0,                          # End Collection                      187
    ]
    # fmt: on

    def __init__(
        self,
        rdesc=report_descriptor,
        name=None,
        input_info=(BusType.USB, 0x06CB, 0x2968),
    ):
        super().__init__(rdesc, name, input_info)

    def event(self, keys, reportID=None, application=None):
        application = application or "Keyboard"
        return super().event(keys, reportID, application)


class TestITEKeyboard(TestArrayKeyboard):
    kernel_modules = [KERNEL_MODULE]

    def create_device(self):
        return ITEKeyboard()

    def test_wifi_key(self):
        uhdev = self.uhdev
        syn_event = self.syn_event

        # the following sends a 'release' event on the Wifi key.
        # the kernel is supposed to translate this into Wifi key
        # down and up
        r = [0x03, 0x00]
        uhdev.call_input_event(r)
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_RFKILL, 1))
        events = uhdev.next_sync_events()
        self.debug_reports([r], uhdev, events)
        self.assertInputEventsIn(expected, events)

        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_KEY.KEY_RFKILL, 0))
        # the kernel sends the two down/up key events in a batch, no need to
        # call events = uhdev.next_sync_events()
        self.debug_reports([], uhdev, events)
        self.assertInputEventsIn(expected, events)
