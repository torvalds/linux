#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2017 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2017 Red Hat, Inc.
#

from . import base
import hidtools.hid
from hidtools.util import BusType
import libevdev
import logging
import pytest

logger = logging.getLogger("hidtools.test.mouse")

# workaround https://gitlab.freedesktop.org/libevdev/python-libevdev/issues/6
try:
    libevdev.EV_REL.REL_WHEEL_HI_RES
except AttributeError:
    libevdev.EV_REL.REL_WHEEL_HI_RES = libevdev.EV_REL.REL_0B
    libevdev.EV_REL.REL_HWHEEL_HI_RES = libevdev.EV_REL.REL_0C


class InvalidHIDCommunication(Exception):
    pass


class MouseData(object):
    pass


class BaseMouse(base.UHIDTestDevice):
    def __init__(self, rdesc, name=None, input_info=None):
        assert rdesc is not None
        super().__init__(name, "Mouse", input_info=input_info, rdesc=rdesc)
        self.left = False
        self.right = False
        self.middle = False

    def create_report(self, x, y, buttons=None, wheels=None, reportID=None):
        """
        Return an input report for this device.

        :param x: relative x
        :param y: relative y
        :param buttons: a (l, r, m) tuple of bools for the button states,
            where ``None`` is "leave unchanged"
        :param wheels: a single value for the vertical wheel or a (vertical, horizontal) tuple for
            the two wheels
        :param reportID: the numeric report ID for this report, if needed
        """
        if buttons is not None:
            l, r, m = buttons
            if l is not None:
                self.left = l
            if r is not None:
                self.right = r
            if m is not None:
                self.middle = m
        left = self.left
        right = self.right
        middle = self.middle
        # Note: the BaseMouse doesn't actually have a wheel but the
        # create_report magic only fills in those fields exist, so let's
        # make this generic here.
        wheel, acpan = 0, 0
        if wheels is not None:
            if isinstance(wheels, tuple):
                wheel = wheels[0]
                acpan = wheels[1]
            else:
                wheel = wheels

        reportID = reportID or self.default_reportID

        mouse = MouseData()
        mouse.b1 = int(left)
        mouse.b2 = int(right)
        mouse.b3 = int(middle)
        mouse.x = x
        mouse.y = y
        mouse.wheel = wheel
        mouse.acpan = acpan
        return super().create_report(mouse, reportID=reportID)

    def event(self, x, y, buttons=None, wheels=None):
        """
        Send an input event on the default report ID.

        :param x: relative x
        :param y: relative y
        :param buttons: a (l, r, m) tuple of bools for the button states,
            where ``None`` is "leave unchanged"
        :param wheels: a single value for the vertical wheel or a (vertical, horizontal) tuple for
            the two wheels
        """
        r = self.create_report(x, y, buttons, wheels)
        self.call_input_event(r)
        return [r]


class ButtonMouse(BaseMouse):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,  # .Usage Page (Generic Desktop)        0
        0x09, 0x02,  # .Usage (Mouse)                       2
        0xa1, 0x01,  # .Collection (Application)            4
        0x09, 0x02,  # ..Usage (Mouse)                      6
        0xa1, 0x02,  # ..Collection (Logical)               8
        0x09, 0x01,  # ...Usage (Pointer)                   10
        0xa1, 0x00,  # ...Collection (Physical)             12
        0x05, 0x09,  # ....Usage Page (Button)              14
        0x19, 0x01,  # ....Usage Minimum (1)                16
        0x29, 0x03,  # ....Usage Maximum (3)                18
        0x15, 0x00,  # ....Logical Minimum (0)              20
        0x25, 0x01,  # ....Logical Maximum (1)              22
        0x75, 0x01,  # ....Report Size (1)                  24
        0x95, 0x03,  # ....Report Count (3)                 26
        0x81, 0x02,  # ....Input (Data,Var,Abs)             28
        0x75, 0x05,  # ....Report Size (5)                  30
        0x95, 0x01,  # ....Report Count (1)                 32
        0x81, 0x03,  # ....Input (Cnst,Var,Abs)             34
        0x05, 0x01,  # ....Usage Page (Generic Desktop)     36
        0x09, 0x30,  # ....Usage (X)                        38
        0x09, 0x31,  # ....Usage (Y)                        40
        0x15, 0x81,  # ....Logical Minimum (-127)           42
        0x25, 0x7f,  # ....Logical Maximum (127)            44
        0x75, 0x08,  # ....Report Size (8)                  46
        0x95, 0x02,  # ....Report Count (2)                 48
        0x81, 0x06,  # ....Input (Data,Var,Rel)             50
        0xc0,        # ...End Collection                    52
        0xc0,        # ..End Collection                     53
        0xc0,        # .End Collection                      54
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, input_info=None):
        super().__init__(rdesc, name, input_info)

    def fake_report(self, x, y, buttons):
        if buttons is not None:
            left, right, middle = buttons
            if left is None:
                left = self.left
            if right is None:
                right = self.right
            if middle is None:
                middle = self.middle
        else:
            left = self.left
            right = self.right
            middle = self.middle

        button_mask = sum(1 << i for i, b in enumerate([left, right, middle]) if b)
        x = max(-127, min(127, x))
        y = max(-127, min(127, y))
        x = hidtools.util.to_twos_comp(x, 8)
        y = hidtools.util.to_twos_comp(y, 8)
        return [button_mask, x, y]


class WheelMouse(ButtonMouse):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,  # Usage Page (Generic Desktop)        0
        0x09, 0x02,  # Usage (Mouse)                       2
        0xa1, 0x01,  # Collection (Application)            4
        0x05, 0x09,  # .Usage Page (Button)                6
        0x19, 0x01,  # .Usage Minimum (1)                  8
        0x29, 0x03,  # .Usage Maximum (3)                  10
        0x15, 0x00,  # .Logical Minimum (0)                12
        0x25, 0x01,  # .Logical Maximum (1)                14
        0x95, 0x03,  # .Report Count (3)                   16
        0x75, 0x01,  # .Report Size (1)                    18
        0x81, 0x02,  # .Input (Data,Var,Abs)               20
        0x95, 0x01,  # .Report Count (1)                   22
        0x75, 0x05,  # .Report Size (5)                    24
        0x81, 0x03,  # .Input (Cnst,Var,Abs)               26
        0x05, 0x01,  # .Usage Page (Generic Desktop)       28
        0x09, 0x01,  # .Usage (Pointer)                    30
        0xa1, 0x00,  # .Collection (Physical)              32
        0x09, 0x30,  # ..Usage (X)                         34
        0x09, 0x31,  # ..Usage (Y)                         36
        0x15, 0x81,  # ..Logical Minimum (-127)            38
        0x25, 0x7f,  # ..Logical Maximum (127)             40
        0x75, 0x08,  # ..Report Size (8)                   42
        0x95, 0x02,  # ..Report Count (2)                  44
        0x81, 0x06,  # ..Input (Data,Var,Rel)              46
        0xc0,        # .End Collection                     48
        0x09, 0x38,  # .Usage (Wheel)                      49
        0x15, 0x81,  # .Logical Minimum (-127)             51
        0x25, 0x7f,  # .Logical Maximum (127)              53
        0x75, 0x08,  # .Report Size (8)                    55
        0x95, 0x01,  # .Report Count (1)                   57
        0x81, 0x06,  # .Input (Data,Var,Rel)               59
        0xc0,        # End Collection                      61
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, input_info=None):
        super().__init__(rdesc, name, input_info)
        self.wheel_multiplier = 1


class TwoWheelMouse(WheelMouse):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,        # Usage Page (Generic Desktop)        0
        0x09, 0x02,        # Usage (Mouse)                       2
        0xa1, 0x01,        # Collection (Application)            4
        0x09, 0x01,        # .Usage (Pointer)                    6
        0xa1, 0x00,        # .Collection (Physical)              8
        0x05, 0x09,        # ..Usage Page (Button)               10
        0x19, 0x01,        # ..Usage Minimum (1)                 12
        0x29, 0x10,        # ..Usage Maximum (16)                14
        0x15, 0x00,        # ..Logical Minimum (0)               16
        0x25, 0x01,        # ..Logical Maximum (1)               18
        0x95, 0x10,        # ..Report Count (16)                 20
        0x75, 0x01,        # ..Report Size (1)                   22
        0x81, 0x02,        # ..Input (Data,Var,Abs)              24
        0x05, 0x01,        # ..Usage Page (Generic Desktop)      26
        0x16, 0x01, 0x80,  # ..Logical Minimum (-32767)          28
        0x26, 0xff, 0x7f,  # ..Logical Maximum (32767)           31
        0x75, 0x10,        # ..Report Size (16)                  34
        0x95, 0x02,        # ..Report Count (2)                  36
        0x09, 0x30,        # ..Usage (X)                         38
        0x09, 0x31,        # ..Usage (Y)                         40
        0x81, 0x06,        # ..Input (Data,Var,Rel)              42
        0x15, 0x81,        # ..Logical Minimum (-127)            44
        0x25, 0x7f,        # ..Logical Maximum (127)             46
        0x75, 0x08,        # ..Report Size (8)                   48
        0x95, 0x01,        # ..Report Count (1)                  50
        0x09, 0x38,        # ..Usage (Wheel)                     52
        0x81, 0x06,        # ..Input (Data,Var,Rel)              54
        0x05, 0x0c,        # ..Usage Page (Consumer Devices)     56
        0x0a, 0x38, 0x02,  # ..Usage (AC Pan)                    58
        0x95, 0x01,        # ..Report Count (1)                  61
        0x81, 0x06,        # ..Input (Data,Var,Rel)              63
        0xc0,              # .End Collection                     65
        0xc0,              # End Collection                      66
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, input_info=None):
        super().__init__(rdesc, name, input_info)
        self.hwheel_multiplier = 1


class MIDongleMIWirelessMouse(TwoWheelMouse):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,         # Usage Page (Generic Desktop)
        0x09, 0x02,         # Usage (Mouse)
        0xa1, 0x01,         # Collection (Application)
        0x85, 0x01,         # .Report ID (1)
        0x09, 0x01,         # .Usage (Pointer)
        0xa1, 0x00,         # .Collection (Physical)
        0x95, 0x05,         # ..Report Count (5)
        0x75, 0x01,         # ..Report Size (1)
        0x05, 0x09,         # ..Usage Page (Button)
        0x19, 0x01,         # ..Usage Minimum (1)
        0x29, 0x05,         # ..Usage Maximum (5)
        0x15, 0x00,         # ..Logical Minimum (0)
        0x25, 0x01,         # ..Logical Maximum (1)
        0x81, 0x02,         # ..Input (Data,Var,Abs)
        0x95, 0x01,         # ..Report Count (1)
        0x75, 0x03,         # ..Report Size (3)
        0x81, 0x01,         # ..Input (Cnst,Arr,Abs)
        0x75, 0x08,         # ..Report Size (8)
        0x95, 0x01,         # ..Report Count (1)
        0x05, 0x01,         # ..Usage Page (Generic Desktop)
        0x09, 0x38,         # ..Usage (Wheel)
        0x15, 0x81,         # ..Logical Minimum (-127)
        0x25, 0x7f,         # ..Logical Maximum (127)
        0x81, 0x06,         # ..Input (Data,Var,Rel)
        0x05, 0x0c,         # ..Usage Page (Consumer Devices)
        0x0a, 0x38, 0x02,   # ..Usage (AC Pan)
        0x95, 0x01,         # ..Report Count (1)
        0x81, 0x06,         # ..Input (Data,Var,Rel)
        0xc0,               # .End Collection
        0x85, 0x02,         # .Report ID (2)
        0x09, 0x01,         # .Usage (Consumer Control)
        0xa1, 0x00,         # .Collection (Physical)
        0x75, 0x0c,         # ..Report Size (12)
        0x95, 0x02,         # ..Report Count (2)
        0x05, 0x01,         # ..Usage Page (Generic Desktop)
        0x09, 0x30,         # ..Usage (X)
        0x09, 0x31,         # ..Usage (Y)
        0x16, 0x01, 0xf8,   # ..Logical Minimum (-2047)
        0x26, 0xff, 0x07,   # ..Logical Maximum (2047)
        0x81, 0x06,         # ..Input (Data,Var,Rel)
        0xc0,               # .End Collection
        0xc0,               # End Collection
        0x05, 0x0c,         # Usage Page (Consumer Devices)
        0x09, 0x01,         # Usage (Consumer Control)
        0xa1, 0x01,         # Collection (Application)
        0x85, 0x03,         # .Report ID (3)
        0x15, 0x00,         # .Logical Minimum (0)
        0x25, 0x01,         # .Logical Maximum (1)
        0x75, 0x01,         # .Report Size (1)
        0x95, 0x01,         # .Report Count (1)
        0x09, 0xcd,         # .Usage (Play/Pause)
        0x81, 0x06,         # .Input (Data,Var,Rel)
        0x0a, 0x83, 0x01,   # .Usage (AL Consumer Control Config)
        0x81, 0x06,         # .Input (Data,Var,Rel)
        0x09, 0xb5,         # .Usage (Scan Next Track)
        0x81, 0x06,         # .Input (Data,Var,Rel)
        0x09, 0xb6,         # .Usage (Scan Previous Track)
        0x81, 0x06,         # .Input (Data,Var,Rel)
        0x09, 0xea,         # .Usage (Volume Down)
        0x81, 0x06,         # .Input (Data,Var,Rel)
        0x09, 0xe9,         # .Usage (Volume Up)
        0x81, 0x06,         # .Input (Data,Var,Rel)
        0x0a, 0x25, 0x02,   # .Usage (AC Forward)
        0x81, 0x06,         # .Input (Data,Var,Rel)
        0x0a, 0x24, 0x02,   # .Usage (AC Back)
        0x81, 0x06,         # .Input (Data,Var,Rel)
        0xc0,               # End Collection
    ]
    # fmt: on
    device_input_info = (BusType.USB, 0x2717, 0x003B)
    device_name = "uhid test MI Dongle MI Wireless Mouse"

    def __init__(
        self, rdesc=report_descriptor, name=device_name, input_info=device_input_info
    ):
        super().__init__(rdesc, name, input_info)

    def event(self, x, y, buttons=None, wheels=None):
        # this mouse spreads the relative pointer and the mouse buttons
        # onto 2 distinct reports
        rs = []
        r = self.create_report(x, y, buttons, wheels, reportID=1)
        self.call_input_event(r)
        rs.append(r)
        r = self.create_report(x, y, buttons, reportID=2)
        self.call_input_event(r)
        rs.append(r)
        return rs


class ResolutionMultiplierMouse(TwoWheelMouse):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,        # Usage Page (Generic Desktop)        83
        0x09, 0x02,        # Usage (Mouse)                       85
        0xa1, 0x01,        # Collection (Application)            87
        0x05, 0x01,        # .Usage Page (Generic Desktop)       89
        0x09, 0x02,        # .Usage (Mouse)                      91
        0xa1, 0x02,        # .Collection (Logical)               93
        0x85, 0x11,        # ..Report ID (17)                    95
        0x09, 0x01,        # ..Usage (Pointer)                   97
        0xa1, 0x00,        # ..Collection (Physical)             99
        0x05, 0x09,        # ...Usage Page (Button)              101
        0x19, 0x01,        # ...Usage Minimum (1)                103
        0x29, 0x03,        # ...Usage Maximum (3)                105
        0x95, 0x03,        # ...Report Count (3)                 107
        0x75, 0x01,        # ...Report Size (1)                  109
        0x25, 0x01,        # ...Logical Maximum (1)              111
        0x81, 0x02,        # ...Input (Data,Var,Abs)             113
        0x95, 0x01,        # ...Report Count (1)                 115
        0x81, 0x01,        # ...Input (Cnst,Arr,Abs)             117
        0x09, 0x05,        # ...Usage (Vendor Usage 0x05)        119
        0x81, 0x02,        # ...Input (Data,Var,Abs)             121
        0x95, 0x03,        # ...Report Count (3)                 123
        0x81, 0x01,        # ...Input (Cnst,Arr,Abs)             125
        0x05, 0x01,        # ...Usage Page (Generic Desktop)     127
        0x09, 0x30,        # ...Usage (X)                        129
        0x09, 0x31,        # ...Usage (Y)                        131
        0x95, 0x02,        # ...Report Count (2)                 133
        0x75, 0x08,        # ...Report Size (8)                  135
        0x15, 0x81,        # ...Logical Minimum (-127)           137
        0x25, 0x7f,        # ...Logical Maximum (127)            139
        0x81, 0x06,        # ...Input (Data,Var,Rel)             141
        0xa1, 0x02,        # ...Collection (Logical)             143
        0x85, 0x12,        # ....Report ID (18)                  145
        0x09, 0x48,        # ....Usage (Resolution Multiplier)   147
        0x95, 0x01,        # ....Report Count (1)                149
        0x75, 0x02,        # ....Report Size (2)                 151
        0x15, 0x00,        # ....Logical Minimum (0)             153
        0x25, 0x01,        # ....Logical Maximum (1)             155
        0x35, 0x01,        # ....Physical Minimum (1)            157
        0x45, 0x04,        # ....Physical Maximum (4)            159
        0xb1, 0x02,        # ....Feature (Data,Var,Abs)          161
        0x35, 0x00,        # ....Physical Minimum (0)            163
        0x45, 0x00,        # ....Physical Maximum (0)            165
        0x75, 0x06,        # ....Report Size (6)                 167
        0xb1, 0x01,        # ....Feature (Cnst,Arr,Abs)          169
        0x85, 0x11,        # ....Report ID (17)                  171
        0x09, 0x38,        # ....Usage (Wheel)                   173
        0x15, 0x81,        # ....Logical Minimum (-127)          175
        0x25, 0x7f,        # ....Logical Maximum (127)           177
        0x75, 0x08,        # ....Report Size (8)                 179
        0x81, 0x06,        # ....Input (Data,Var,Rel)            181
        0xc0,              # ...End Collection                   183
        0x05, 0x0c,        # ...Usage Page (Consumer Devices)    184
        0x75, 0x08,        # ...Report Size (8)                  186
        0x0a, 0x38, 0x02,  # ...Usage (AC Pan)                   188
        0x81, 0x06,        # ...Input (Data,Var,Rel)             191
        0xc0,              # ..End Collection                    193
        0xc0,              # .End Collection                     194
        0xc0,              # End Collection                      195
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, input_info=None):
        super().__init__(rdesc, name, input_info)
        self.default_reportID = 0x11

        # Feature Report 12, multiplier Feature value must be set to 0b01,
        # i.e. 1. We should extract that from the descriptor instead
        # of hardcoding it here, but meanwhile this will do.
        self.set_feature_report = [0x12, 0x1]

    def set_report(self, req, rnum, rtype, data):
        if rtype != self.UHID_FEATURE_REPORT:
            raise InvalidHIDCommunication(f"Unexpected report type: {rtype}")
        if rnum != 0x12:
            raise InvalidHIDCommunication(f"Unexpected report number: {rnum}")

        if data != self.set_feature_report:
            raise InvalidHIDCommunication(
                f"Unexpected data: {data}, expected {self.set_feature_report}"
            )

        self.wheel_multiplier = 4

        return 0


class BadResolutionMultiplierMouse(ResolutionMultiplierMouse):
    def set_report(self, req, rnum, rtype, data):
        super().set_report(req, rnum, rtype, data)

        self.wheel_multiplier = 1
        self.hwheel_multiplier = 1
        return 32  # EPIPE


class ResolutionMultiplierHWheelMouse(TwoWheelMouse):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,         # Usage Page (Generic Desktop)        0
        0x09, 0x02,         # Usage (Mouse)                       2
        0xa1, 0x01,         # Collection (Application)            4
        0x05, 0x01,         # .Usage Page (Generic Desktop)       6
        0x09, 0x02,         # .Usage (Mouse)                      8
        0xa1, 0x02,         # .Collection (Logical)               10
        0x85, 0x1a,         # ..Report ID (26)                    12
        0x09, 0x01,         # ..Usage (Pointer)                   14
        0xa1, 0x00,         # ..Collection (Physical)             16
        0x05, 0x09,         # ...Usage Page (Button)              18
        0x19, 0x01,         # ...Usage Minimum (1)                20
        0x29, 0x05,         # ...Usage Maximum (5)                22
        0x95, 0x05,         # ...Report Count (5)                 24
        0x75, 0x01,         # ...Report Size (1)                  26
        0x15, 0x00,         # ...Logical Minimum (0)              28
        0x25, 0x01,         # ...Logical Maximum (1)              30
        0x81, 0x02,         # ...Input (Data,Var,Abs)             32
        0x75, 0x03,         # ...Report Size (3)                  34
        0x95, 0x01,         # ...Report Count (1)                 36
        0x81, 0x01,         # ...Input (Cnst,Arr,Abs)             38
        0x05, 0x01,         # ...Usage Page (Generic Desktop)     40
        0x09, 0x30,         # ...Usage (X)                        42
        0x09, 0x31,         # ...Usage (Y)                        44
        0x95, 0x02,         # ...Report Count (2)                 46
        0x75, 0x10,         # ...Report Size (16)                 48
        0x16, 0x01, 0x80,   # ...Logical Minimum (-32767)         50
        0x26, 0xff, 0x7f,   # ...Logical Maximum (32767)          53
        0x81, 0x06,         # ...Input (Data,Var,Rel)             56
        0xa1, 0x02,         # ...Collection (Logical)             58
        0x85, 0x12,         # ....Report ID (18)                  60
        0x09, 0x48,         # ....Usage (Resolution Multiplier)   62
        0x95, 0x01,         # ....Report Count (1)                64
        0x75, 0x02,         # ....Report Size (2)                 66
        0x15, 0x00,         # ....Logical Minimum (0)             68
        0x25, 0x01,         # ....Logical Maximum (1)             70
        0x35, 0x01,         # ....Physical Minimum (1)            72
        0x45, 0x0c,         # ....Physical Maximum (12)           74
        0xb1, 0x02,         # ....Feature (Data,Var,Abs)          76
        0x85, 0x1a,         # ....Report ID (26)                  78
        0x09, 0x38,         # ....Usage (Wheel)                   80
        0x35, 0x00,         # ....Physical Minimum (0)            82
        0x45, 0x00,         # ....Physical Maximum (0)            84
        0x95, 0x01,         # ....Report Count (1)                86
        0x75, 0x10,         # ....Report Size (16)                88
        0x16, 0x01, 0x80,   # ....Logical Minimum (-32767)        90
        0x26, 0xff, 0x7f,   # ....Logical Maximum (32767)         93
        0x81, 0x06,         # ....Input (Data,Var,Rel)            96
        0xc0,               # ...End Collection                   98
        0xa1, 0x02,         # ...Collection (Logical)             99
        0x85, 0x12,         # ....Report ID (18)                  101
        0x09, 0x48,         # ....Usage (Resolution Multiplier)   103
        0x75, 0x02,         # ....Report Size (2)                 105
        0x15, 0x00,         # ....Logical Minimum (0)             107
        0x25, 0x01,         # ....Logical Maximum (1)             109
        0x35, 0x01,         # ....Physical Minimum (1)            111
        0x45, 0x0c,         # ....Physical Maximum (12)           113
        0xb1, 0x02,         # ....Feature (Data,Var,Abs)          115
        0x35, 0x00,         # ....Physical Minimum (0)            117
        0x45, 0x00,         # ....Physical Maximum (0)            119
        0x75, 0x04,         # ....Report Size (4)                 121
        0xb1, 0x01,         # ....Feature (Cnst,Arr,Abs)          123
        0x85, 0x1a,         # ....Report ID (26)                  125
        0x05, 0x0c,         # ....Usage Page (Consumer Devices)   127
        0x95, 0x01,         # ....Report Count (1)                129
        0x75, 0x10,         # ....Report Size (16)                131
        0x16, 0x01, 0x80,   # ....Logical Minimum (-32767)        133
        0x26, 0xff, 0x7f,   # ....Logical Maximum (32767)         136
        0x0a, 0x38, 0x02,   # ....Usage (AC Pan)                  139
        0x81, 0x06,         # ....Input (Data,Var,Rel)            142
        0xc0,               # ...End Collection                   144
        0xc0,               # ..End Collection                    145
        0xc0,               # .End Collection                     146
        0xc0,               # End Collection                      147
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, input_info=None):
        super().__init__(rdesc, name, input_info)
        self.default_reportID = 0x1A

        # Feature Report 12, multiplier Feature value must be set to 0b0101,
        # i.e. 5. We should extract that from the descriptor instead
        # of hardcoding it here, but meanwhile this will do.
        self.set_feature_report = [0x12, 0x5]

    def set_report(self, req, rnum, rtype, data):
        super().set_report(req, rnum, rtype, data)

        self.wheel_multiplier = 12
        self.hwheel_multiplier = 12

        return 0


class BaseTest:
    class TestMouse(base.BaseTestCase.TestUhid):
        def test_buttons(self):
            """check for button reliability."""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()
            syn_event = self.syn_event

            r = uhdev.event(0, 0, (None, True, None))
            expected_event = libevdev.InputEvent(libevdev.EV_KEY.BTN_RIGHT, 1)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[libevdev.EV_KEY.BTN_RIGHT] == 1

            r = uhdev.event(0, 0, (None, False, None))
            expected_event = libevdev.InputEvent(libevdev.EV_KEY.BTN_RIGHT, 0)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[libevdev.EV_KEY.BTN_RIGHT] == 0

            r = uhdev.event(0, 0, (None, None, True))
            expected_event = libevdev.InputEvent(libevdev.EV_KEY.BTN_MIDDLE, 1)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[libevdev.EV_KEY.BTN_MIDDLE] == 1

            r = uhdev.event(0, 0, (None, None, False))
            expected_event = libevdev.InputEvent(libevdev.EV_KEY.BTN_MIDDLE, 0)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[libevdev.EV_KEY.BTN_MIDDLE] == 0

            r = uhdev.event(0, 0, (True, None, None))
            expected_event = libevdev.InputEvent(libevdev.EV_KEY.BTN_LEFT, 1)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[libevdev.EV_KEY.BTN_LEFT] == 1

            r = uhdev.event(0, 0, (False, None, None))
            expected_event = libevdev.InputEvent(libevdev.EV_KEY.BTN_LEFT, 0)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[libevdev.EV_KEY.BTN_LEFT] == 0

            r = uhdev.event(0, 0, (True, True, None))
            expected_event0 = libevdev.InputEvent(libevdev.EV_KEY.BTN_LEFT, 1)
            expected_event1 = libevdev.InputEvent(libevdev.EV_KEY.BTN_RIGHT, 1)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn(
                (syn_event, expected_event0, expected_event1), events
            )
            assert evdev.value[libevdev.EV_KEY.BTN_RIGHT] == 1
            assert evdev.value[libevdev.EV_KEY.BTN_LEFT] == 1

            r = uhdev.event(0, 0, (False, None, None))
            expected_event = libevdev.InputEvent(libevdev.EV_KEY.BTN_LEFT, 0)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[libevdev.EV_KEY.BTN_RIGHT] == 1
            assert evdev.value[libevdev.EV_KEY.BTN_LEFT] == 0

            r = uhdev.event(0, 0, (None, False, None))
            expected_event = libevdev.InputEvent(libevdev.EV_KEY.BTN_RIGHT, 0)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[libevdev.EV_KEY.BTN_RIGHT] == 0
            assert evdev.value[libevdev.EV_KEY.BTN_LEFT] == 0

        def test_relative(self):
            """Check for relative events."""
            uhdev = self.uhdev

            syn_event = self.syn_event

            r = uhdev.event(0, -1)
            expected_event = libevdev.InputEvent(libevdev.EV_REL.REL_Y, -1)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEvents((syn_event, expected_event), events)

            r = uhdev.event(1, 0)
            expected_event = libevdev.InputEvent(libevdev.EV_REL.REL_X, 1)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEvents((syn_event, expected_event), events)

            r = uhdev.event(-1, 2)
            expected_event0 = libevdev.InputEvent(libevdev.EV_REL.REL_X, -1)
            expected_event1 = libevdev.InputEvent(libevdev.EV_REL.REL_Y, 2)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEvents(
                (syn_event, expected_event0, expected_event1), events
            )


class TestSimpleMouse(BaseTest.TestMouse):
    def create_device(self):
        return ButtonMouse()

    def test_rdesc(self):
        """Check that the testsuite actually manages to format the
        reports according to the report descriptors.
        No kernel device is used here"""
        uhdev = self.uhdev

        event = (0, 0, (None, None, None))
        assert uhdev.fake_report(*event) == uhdev.create_report(*event)

        event = (0, 0, (None, True, None))
        assert uhdev.fake_report(*event) == uhdev.create_report(*event)

        event = (0, 0, (True, True, None))
        assert uhdev.fake_report(*event) == uhdev.create_report(*event)

        event = (0, 0, (False, False, False))
        assert uhdev.fake_report(*event) == uhdev.create_report(*event)

        event = (1, 0, (True, False, True))
        assert uhdev.fake_report(*event) == uhdev.create_report(*event)

        event = (-1, 0, (True, False, True))
        assert uhdev.fake_report(*event) == uhdev.create_report(*event)

        event = (-5, 5, (True, False, True))
        assert uhdev.fake_report(*event) == uhdev.create_report(*event)

        event = (-127, 127, (True, False, True))
        assert uhdev.fake_report(*event) == uhdev.create_report(*event)

        event = (0, -128, (True, False, True))
        with pytest.raises(hidtools.hid.RangeError):
            uhdev.create_report(*event)


class TestWheelMouse(BaseTest.TestMouse):
    def create_device(self):
        return WheelMouse()

    def is_wheel_highres(self, uhdev):
        evdev = uhdev.get_evdev()
        assert evdev.has(libevdev.EV_REL.REL_WHEEL)
        return evdev.has(libevdev.EV_REL.REL_WHEEL_HI_RES)

    def test_wheel(self):
        uhdev = self.uhdev

        # check if the kernel is high res wheel compatible
        high_res_wheel = self.is_wheel_highres(uhdev)

        syn_event = self.syn_event
        # The Resolution Multiplier is applied to the HID reports, so we
        # need to pre-multiply too.
        mult = uhdev.wheel_multiplier

        r = uhdev.event(0, 0, wheels=1 * mult)
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL, 1))
        if high_res_wheel:
            expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL_HI_RES, 120))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)

        r = uhdev.event(0, 0, wheels=-1 * mult)
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL, -1))
        if high_res_wheel:
            expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL_HI_RES, -120))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)

        r = uhdev.event(-1, 2, wheels=3 * mult)
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_X, -1))
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_Y, 2))
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL, 3))
        if high_res_wheel:
            expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL_HI_RES, 360))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)


class TestTwoWheelMouse(TestWheelMouse):
    def create_device(self):
        return TwoWheelMouse()

    def is_hwheel_highres(self, uhdev):
        evdev = uhdev.get_evdev()
        assert evdev.has(libevdev.EV_REL.REL_HWHEEL)
        return evdev.has(libevdev.EV_REL.REL_HWHEEL_HI_RES)

    def test_ac_pan(self):
        uhdev = self.uhdev

        # check if the kernel is high res wheel compatible
        high_res_wheel = self.is_wheel_highres(uhdev)
        high_res_hwheel = self.is_hwheel_highres(uhdev)
        assert high_res_wheel == high_res_hwheel

        syn_event = self.syn_event
        # The Resolution Multiplier is applied to the HID reports, so we
        # need to pre-multiply too.
        hmult = uhdev.hwheel_multiplier
        vmult = uhdev.wheel_multiplier

        r = uhdev.event(0, 0, wheels=(0, 1 * hmult))
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL, 1))
        if high_res_hwheel:
            expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL_HI_RES, 120))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)

        r = uhdev.event(0, 0, wheels=(0, -1 * hmult))
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL, -1))
        if high_res_hwheel:
            expected.append(
                libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL_HI_RES, -120)
            )
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)

        r = uhdev.event(-1, 2, wheels=(0, 3 * hmult))
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_X, -1))
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_Y, 2))
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL, 3))
        if high_res_hwheel:
            expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL_HI_RES, 360))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)

        r = uhdev.event(-1, 2, wheels=(-3 * vmult, 4 * hmult))
        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_X, -1))
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_Y, 2))
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL, -3))
        if high_res_wheel:
            expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL_HI_RES, -360))
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL, 4))
        if high_res_wheel:
            expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL_HI_RES, 480))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)


class TestResolutionMultiplierMouse(TestTwoWheelMouse):
    def create_device(self):
        return ResolutionMultiplierMouse()

    def is_wheel_highres(self, uhdev):
        high_res = super().is_wheel_highres(uhdev)

        if not high_res:
            # the kernel doesn't seem to support the high res wheel mice,
            # make sure we haven't triggered the feature
            assert uhdev.wheel_multiplier == 1

        return high_res

    def test_resolution_multiplier_wheel(self):
        uhdev = self.uhdev

        if not self.is_wheel_highres(uhdev):
            pytest.skip("Kernel not compatible, we can not trigger the conditions")

        assert uhdev.wheel_multiplier > 1
        assert 120 % uhdev.wheel_multiplier == 0

    def test_wheel_with_multiplier(self):
        uhdev = self.uhdev

        if not self.is_wheel_highres(uhdev):
            pytest.skip("Kernel not compatible, we can not trigger the conditions")

        assert uhdev.wheel_multiplier > 1

        syn_event = self.syn_event
        mult = uhdev.wheel_multiplier

        r = uhdev.event(0, 0, wheels=1)
        expected = [syn_event]
        expected.append(
            libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL_HI_RES, 120 / mult)
        )
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)

        r = uhdev.event(0, 0, wheels=-1)
        expected = [syn_event]
        expected.append(
            libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL_HI_RES, -120 / mult)
        )
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)

        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_X, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_Y, -2))
        expected.append(
            libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL_HI_RES, 120 / mult)
        )

        for _ in range(mult - 1):
            r = uhdev.event(1, -2, wheels=1)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEvents(expected, events)

        r = uhdev.event(1, -2, wheels=1)
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_WHEEL, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)


class TestBadResolutionMultiplierMouse(TestTwoWheelMouse):
    def create_device(self):
        return BadResolutionMultiplierMouse()

    def is_wheel_highres(self, uhdev):
        high_res = super().is_wheel_highres(uhdev)

        assert uhdev.wheel_multiplier == 1

        return high_res

    def test_resolution_multiplier_wheel(self):
        uhdev = self.uhdev

        assert uhdev.wheel_multiplier == 1


class TestResolutionMultiplierHWheelMouse(TestResolutionMultiplierMouse):
    def create_device(self):
        return ResolutionMultiplierHWheelMouse()

    def is_hwheel_highres(self, uhdev):
        high_res = super().is_hwheel_highres(uhdev)

        if not high_res:
            # the kernel doesn't seem to support the high res wheel mice,
            # make sure we haven't triggered the feature
            assert uhdev.hwheel_multiplier == 1

        return high_res

    def test_resolution_multiplier_ac_pan(self):
        uhdev = self.uhdev

        if not self.is_hwheel_highres(uhdev):
            pytest.skip("Kernel not compatible, we can not trigger the conditions")

        assert uhdev.hwheel_multiplier > 1
        assert 120 % uhdev.hwheel_multiplier == 0

    def test_ac_pan_with_multiplier(self):
        uhdev = self.uhdev

        if not self.is_hwheel_highres(uhdev):
            pytest.skip("Kernel not compatible, we can not trigger the conditions")

        assert uhdev.hwheel_multiplier > 1

        syn_event = self.syn_event
        hmult = uhdev.hwheel_multiplier

        r = uhdev.event(0, 0, wheels=(0, 1))
        expected = [syn_event]
        expected.append(
            libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL_HI_RES, 120 / hmult)
        )
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)

        r = uhdev.event(0, 0, wheels=(0, -1))
        expected = [syn_event]
        expected.append(
            libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL_HI_RES, -120 / hmult)
        )
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)

        expected = [syn_event]
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_X, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_Y, -2))
        expected.append(
            libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL_HI_RES, 120 / hmult)
        )

        for _ in range(hmult - 1):
            r = uhdev.event(1, -2, wheels=(0, 1))
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEvents(expected, events)

        r = uhdev.event(1, -2, wheels=(0, 1))
        expected.append(libevdev.InputEvent(libevdev.EV_REL.REL_HWHEEL, 1))
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        self.assertInputEvents(expected, events)


class TestMiMouse(TestWheelMouse):
    def create_device(self):
        return MIDongleMIWirelessMouse()

    def assertInputEvents(self, expected_events, effective_events):
        # Buttons and x/y are spread over two HID reports, so we can get two
        # event frames for this device.
        remaining = self.assertInputEventsIn(expected_events, effective_events)
        try:
            remaining.remove(libevdev.InputEvent(libevdev.EV_SYN.SYN_REPORT, 0))
        except ValueError:
            # If there's no SYN_REPORT in the list, continue and let the
            # assert below print out the real error
            pass
        assert remaining == []
