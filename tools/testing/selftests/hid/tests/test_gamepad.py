#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2019 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2019 Red Hat, Inc.
#

from . import base
import libevdev
import pytest

from .base_gamepad import BaseGamepad, JoystickGamepad, AxisMapping
from hidtools.util import BusType

import logging

logger = logging.getLogger("hidtools.test.gamepad")


class BaseTest:
    class TestGamepad(base.BaseTestCase.TestUhid):
        @pytest.fixture(autouse=True)
        def send_initial_state(self):
            """send an empty report to initialize the axes"""
            uhdev = self.uhdev

            r = uhdev.event()
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)

        def assert_button(self, button):
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()
            syn_event = self.syn_event

            buttons = {}
            key = libevdev.evbit(uhdev.buttons_map[button])

            buttons[button] = True
            r = uhdev.event(buttons=buttons)
            expected_event = libevdev.InputEvent(key, 1)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[key] == 1

            buttons[button] = False
            r = uhdev.event(buttons=buttons)
            expected_event = libevdev.InputEvent(key, 0)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[key] == 0

        def test_buttons(self):
            """check for button reliability."""
            uhdev = self.uhdev

            for b in uhdev.buttons:
                self.assert_button(b)

        def test_dual_buttons(self):
            """check for button reliability when pressing 2 buttons"""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()
            syn_event = self.syn_event

            # can change intended b1 b2 values
            b1 = uhdev.buttons[0]
            key1 = libevdev.evbit(uhdev.buttons_map[b1])
            b2 = uhdev.buttons[1]
            key2 = libevdev.evbit(uhdev.buttons_map[b2])

            buttons = {b1: True, b2: True}
            r = uhdev.event(buttons=buttons)
            expected_event0 = libevdev.InputEvent(key1, 1)
            expected_event1 = libevdev.InputEvent(key2, 1)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn(
                (syn_event, expected_event0, expected_event1), events
            )
            assert evdev.value[key1] == 1
            assert evdev.value[key2] == 1

            buttons = {b1: False, b2: None}
            r = uhdev.event(buttons=buttons)
            expected_event = libevdev.InputEvent(key1, 0)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[key1] == 0
            assert evdev.value[key2] == 1

            buttons = {b1: None, b2: False}
            r = uhdev.event(buttons=buttons)
            expected_event = libevdev.InputEvent(key2, 0)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            self.assertInputEventsIn((syn_event, expected_event), events)
            assert evdev.value[key1] == 0
            assert evdev.value[key2] == 0

        def _get_libevdev_abs_events(self, which):
            """Returns which ABS_* evdev axes are expected for the given stick"""
            abs_map = self.uhdev.axes_map[which]

            x = abs_map["x"].evdev
            y = abs_map["y"].evdev

            assert x
            assert y

            return x, y

        def _test_joystick_press(self, which, data):
            uhdev = self.uhdev

            libevdev_axes = self._get_libevdev_abs_events(which)

            r = None
            if which == "left_stick":
                r = uhdev.event(left=data)
            else:
                r = uhdev.event(right=data)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)

            for i, d in enumerate(data):
                if d is not None and d != 127:
                    assert libevdev.InputEvent(libevdev_axes[i], d) in events
                else:
                    assert libevdev.InputEvent(libevdev_axes[i]) not in events

        def test_left_joystick_press_left(self):
            """check for the left joystick reliability"""
            self._test_joystick_press("left_stick", (63, None))
            self._test_joystick_press("left_stick", (0, 127))

        def test_left_joystick_press_right(self):
            """check for the left joystick reliability"""
            self._test_joystick_press("left_stick", (191, 127))
            self._test_joystick_press("left_stick", (255, None))

        def test_left_joystick_press_up(self):
            """check for the left joystick reliability"""
            self._test_joystick_press("left_stick", (None, 63))
            self._test_joystick_press("left_stick", (127, 0))

        def test_left_joystick_press_down(self):
            """check for the left joystick reliability"""
            self._test_joystick_press("left_stick", (127, 191))
            self._test_joystick_press("left_stick", (None, 255))

        def test_right_joystick_press_left(self):
            """check for the right joystick reliability"""
            self._test_joystick_press("right_stick", (63, None))
            self._test_joystick_press("right_stick", (0, 127))

        def test_right_joystick_press_right(self):
            """check for the right joystick reliability"""
            self._test_joystick_press("right_stick", (191, 127))
            self._test_joystick_press("right_stick", (255, None))

        def test_right_joystick_press_up(self):
            """check for the right joystick reliability"""
            self._test_joystick_press("right_stick", (None, 63))
            self._test_joystick_press("right_stick", (127, 0))

        def test_right_joystick_press_down(self):
            """check for the right joystick reliability"""
            self._test_joystick_press("right_stick", (127, 191))
            self._test_joystick_press("right_stick", (None, 255))

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: "Hat switch" not in uhdev.fields,
            "Device not compatible, missing Hat switch usage",
        )
        @pytest.mark.parametrize(
            "hat_value,expected_evdev,evdev_value",
            [
                (0, "ABS_HAT0Y", -1),
                (2, "ABS_HAT0X", 1),
                (4, "ABS_HAT0Y", 1),
                (6, "ABS_HAT0X", -1),
            ],
        )
        def test_hat_switch(self, hat_value, expected_evdev, evdev_value):
            uhdev = self.uhdev

            r = uhdev.event(hat_switch=hat_value)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert (
                libevdev.InputEvent(
                    libevdev.evbit("EV_ABS", expected_evdev), evdev_value
                )
                in events
            )


class SaitekGamepad(JoystickGamepad):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,                    # Usage Page (Generic Desktop)        0
        0x09, 0x04,                    # Usage (Joystick)                    2
        0xa1, 0x01,                    # Collection (Application)            4
        0x09, 0x01,                    # .Usage (Pointer)                    6
        0xa1, 0x00,                    # .Collection (Physical)              8
        0x85, 0x01,                    # ..Report ID (1)                     10
        0x09, 0x30,                    # ..Usage (X)                         12
        0x15, 0x00,                    # ..Logical Minimum (0)               14
        0x26, 0xff, 0x00,              # ..Logical Maximum (255)             16
        0x35, 0x00,                    # ..Physical Minimum (0)              19
        0x46, 0xff, 0x00,              # ..Physical Maximum (255)            21
        0x75, 0x08,                    # ..Report Size (8)                   24
        0x95, 0x01,                    # ..Report Count (1)                  26
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              28
        0x09, 0x31,                    # ..Usage (Y)                         30
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              32
        0x05, 0x02,                    # ..Usage Page (Simulation Controls)  34
        0x09, 0xba,                    # ..Usage (Rudder)                    36
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              38
        0x09, 0xbb,                    # ..Usage (Throttle)                  40
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              42
        0x05, 0x09,                    # ..Usage Page (Button)               44
        0x19, 0x01,                    # ..Usage Minimum (1)                 46
        0x29, 0x0c,                    # ..Usage Maximum (12)                48
        0x25, 0x01,                    # ..Logical Maximum (1)               50
        0x45, 0x01,                    # ..Physical Maximum (1)              52
        0x75, 0x01,                    # ..Report Size (1)                   54
        0x95, 0x0c,                    # ..Report Count (12)                 56
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              58
        0x95, 0x01,                    # ..Report Count (1)                  60
        0x75, 0x00,                    # ..Report Size (0)                   62
        0x81, 0x03,                    # ..Input (Cnst,Var,Abs)              64
        0x05, 0x01,                    # ..Usage Page (Generic Desktop)      66
        0x09, 0x39,                    # ..Usage (Hat switch)                68
        0x25, 0x07,                    # ..Logical Maximum (7)               70
        0x46, 0x3b, 0x01,              # ..Physical Maximum (315)            72
        0x55, 0x00,                    # ..Unit Exponent (0)                 75
        0x65, 0x44,                    # ..Unit (Degrees^4,EngRotation)      77
        0x75, 0x04,                    # ..Report Size (4)                   79
        0x81, 0x42,                    # ..Input (Data,Var,Abs,Null)         81
        0x65, 0x00,                    # ..Unit (None)                       83
        0xc0,                          # .End Collection                     85
        0x05, 0x0f,                    # .Usage Page (Vendor Usage Page 0x0f) 86
        0x09, 0x92,                    # .Usage (Vendor Usage 0x92)          88
        0xa1, 0x02,                    # .Collection (Logical)               90
        0x85, 0x02,                    # ..Report ID (2)                     92
        0x09, 0xa0,                    # ..Usage (Vendor Usage 0xa0)         94
        0x09, 0x9f,                    # ..Usage (Vendor Usage 0x9f)         96
        0x25, 0x01,                    # ..Logical Maximum (1)               98
        0x45, 0x00,                    # ..Physical Maximum (0)              100
        0x75, 0x01,                    # ..Report Size (1)                   102
        0x95, 0x02,                    # ..Report Count (2)                  104
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              106
        0x75, 0x06,                    # ..Report Size (6)                   108
        0x95, 0x01,                    # ..Report Count (1)                  110
        0x81, 0x03,                    # ..Input (Cnst,Var,Abs)              112
        0x09, 0x22,                    # ..Usage (Vendor Usage 0x22)         114
        0x75, 0x07,                    # ..Report Size (7)                   116
        0x25, 0x7f,                    # ..Logical Maximum (127)             118
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              120
        0x09, 0x94,                    # ..Usage (Vendor Usage 0x94)         122
        0x75, 0x01,                    # ..Report Size (1)                   124
        0x25, 0x01,                    # ..Logical Maximum (1)               126
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              128
        0xc0,                          # .End Collection                     130
        0x09, 0x21,                    # .Usage (Vendor Usage 0x21)          131
        0xa1, 0x02,                    # .Collection (Logical)               133
        0x85, 0x0b,                    # ..Report ID (11)                    135
        0x09, 0x22,                    # ..Usage (Vendor Usage 0x22)         137
        0x26, 0xff, 0x00,              # ..Logical Maximum (255)             139
        0x75, 0x08,                    # ..Report Size (8)                   142
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             144
        0x09, 0x53,                    # ..Usage (Vendor Usage 0x53)         146
        0x25, 0x0a,                    # ..Logical Maximum (10)              148
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             150
        0x09, 0x50,                    # ..Usage (Vendor Usage 0x50)         152
        0x27, 0xfe, 0xff, 0x00, 0x00,  # ..Logical Maximum (65534)           154
        0x47, 0xfe, 0xff, 0x00, 0x00,  # ..Physical Maximum (65534)          159
        0x75, 0x10,                    # ..Report Size (16)                  164
        0x55, 0xfd,                    # ..Unit Exponent (237)               166
        0x66, 0x01, 0x10,              # ..Unit (Seconds,SILinear)           168
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             171
        0x55, 0x00,                    # ..Unit Exponent (0)                 173
        0x65, 0x00,                    # ..Unit (None)                       175
        0x09, 0x54,                    # ..Usage (Vendor Usage 0x54)         177
        0x55, 0xfd,                    # ..Unit Exponent (237)               179
        0x66, 0x01, 0x10,              # ..Unit (Seconds,SILinear)           181
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             184
        0x55, 0x00,                    # ..Unit Exponent (0)                 186
        0x65, 0x00,                    # ..Unit (None)                       188
        0x09, 0xa7,                    # ..Usage (Vendor Usage 0xa7)         190
        0x55, 0xfd,                    # ..Unit Exponent (237)               192
        0x66, 0x01, 0x10,              # ..Unit (Seconds,SILinear)           194
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             197
        0x55, 0x00,                    # ..Unit Exponent (0)                 199
        0x65, 0x00,                    # ..Unit (None)                       201
        0xc0,                          # .End Collection                     203
        0x09, 0x5a,                    # .Usage (Vendor Usage 0x5a)          204
        0xa1, 0x02,                    # .Collection (Logical)               206
        0x85, 0x0c,                    # ..Report ID (12)                    208
        0x09, 0x22,                    # ..Usage (Vendor Usage 0x22)         210
        0x26, 0xff, 0x00,              # ..Logical Maximum (255)             212
        0x45, 0x00,                    # ..Physical Maximum (0)              215
        0x75, 0x08,                    # ..Report Size (8)                   217
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             219
        0x09, 0x5c,                    # ..Usage (Vendor Usage 0x5c)         221
        0x26, 0x10, 0x27,              # ..Logical Maximum (10000)           223
        0x46, 0x10, 0x27,              # ..Physical Maximum (10000)          226
        0x75, 0x10,                    # ..Report Size (16)                  229
        0x55, 0xfd,                    # ..Unit Exponent (237)               231
        0x66, 0x01, 0x10,              # ..Unit (Seconds,SILinear)           233
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             236
        0x55, 0x00,                    # ..Unit Exponent (0)                 238
        0x65, 0x00,                    # ..Unit (None)                       240
        0x09, 0x5b,                    # ..Usage (Vendor Usage 0x5b)         242
        0x25, 0x7f,                    # ..Logical Maximum (127)             244
        0x75, 0x08,                    # ..Report Size (8)                   246
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             248
        0x09, 0x5e,                    # ..Usage (Vendor Usage 0x5e)         250
        0x26, 0x10, 0x27,              # ..Logical Maximum (10000)           252
        0x75, 0x10,                    # ..Report Size (16)                  255
        0x55, 0xfd,                    # ..Unit Exponent (237)               257
        0x66, 0x01, 0x10,              # ..Unit (Seconds,SILinear)           259
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             262
        0x55, 0x00,                    # ..Unit Exponent (0)                 264
        0x65, 0x00,                    # ..Unit (None)                       266
        0x09, 0x5d,                    # ..Usage (Vendor Usage 0x5d)         268
        0x25, 0x7f,                    # ..Logical Maximum (127)             270
        0x75, 0x08,                    # ..Report Size (8)                   272
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             274
        0xc0,                          # .End Collection                     276
        0x09, 0x73,                    # .Usage (Vendor Usage 0x73)          277
        0xa1, 0x02,                    # .Collection (Logical)               279
        0x85, 0x0d,                    # ..Report ID (13)                    281
        0x09, 0x22,                    # ..Usage (Vendor Usage 0x22)         283
        0x26, 0xff, 0x00,              # ..Logical Maximum (255)             285
        0x45, 0x00,                    # ..Physical Maximum (0)              288
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             290
        0x09, 0x70,                    # ..Usage (Vendor Usage 0x70)         292
        0x15, 0x81,                    # ..Logical Minimum (-127)            294
        0x25, 0x7f,                    # ..Logical Maximum (127)             296
        0x36, 0xf0, 0xd8,              # ..Physical Minimum (-10000)         298
        0x46, 0x10, 0x27,              # ..Physical Maximum (10000)          301
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             304
        0xc0,                          # .End Collection                     306
        0x09, 0x6e,                    # .Usage (Vendor Usage 0x6e)          307
        0xa1, 0x02,                    # .Collection (Logical)               309
        0x85, 0x0e,                    # ..Report ID (14)                    311
        0x09, 0x22,                    # ..Usage (Vendor Usage 0x22)         313
        0x15, 0x00,                    # ..Logical Minimum (0)               315
        0x26, 0xff, 0x00,              # ..Logical Maximum (255)             317
        0x35, 0x00,                    # ..Physical Minimum (0)              320
        0x45, 0x00,                    # ..Physical Maximum (0)              322
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             324
        0x09, 0x70,                    # ..Usage (Vendor Usage 0x70)         326
        0x25, 0x7f,                    # ..Logical Maximum (127)             328
        0x46, 0x10, 0x27,              # ..Physical Maximum (10000)          330
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             333
        0x09, 0x6f,                    # ..Usage (Vendor Usage 0x6f)         335
        0x15, 0x81,                    # ..Logical Minimum (-127)            337
        0x36, 0xf0, 0xd8,              # ..Physical Minimum (-10000)         339
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             342
        0x09, 0x71,                    # ..Usage (Vendor Usage 0x71)         344
        0x15, 0x00,                    # ..Logical Minimum (0)               346
        0x26, 0xff, 0x00,              # ..Logical Maximum (255)             348
        0x35, 0x00,                    # ..Physical Minimum (0)              351
        0x46, 0x68, 0x01,              # ..Physical Maximum (360)            353
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             356
        0x09, 0x72,                    # ..Usage (Vendor Usage 0x72)         358
        0x75, 0x10,                    # ..Report Size (16)                  360
        0x26, 0x10, 0x27,              # ..Logical Maximum (10000)           362
        0x46, 0x10, 0x27,              # ..Physical Maximum (10000)          365
        0x55, 0xfd,                    # ..Unit Exponent (237)               368
        0x66, 0x01, 0x10,              # ..Unit (Seconds,SILinear)           370
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             373
        0x55, 0x00,                    # ..Unit Exponent (0)                 375
        0x65, 0x00,                    # ..Unit (None)                       377
        0xc0,                          # .End Collection                     379
        0x09, 0x77,                    # .Usage (Vendor Usage 0x77)          380
        0xa1, 0x02,                    # .Collection (Logical)               382
        0x85, 0x51,                    # ..Report ID (81)                    384
        0x09, 0x22,                    # ..Usage (Vendor Usage 0x22)         386
        0x25, 0x7f,                    # ..Logical Maximum (127)             388
        0x45, 0x00,                    # ..Physical Maximum (0)              390
        0x75, 0x08,                    # ..Report Size (8)                   392
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             394
        0x09, 0x78,                    # ..Usage (Vendor Usage 0x78)         396
        0xa1, 0x02,                    # ..Collection (Logical)              398
        0x09, 0x7b,                    # ...Usage (Vendor Usage 0x7b)        400
        0x09, 0x79,                    # ...Usage (Vendor Usage 0x79)        402
        0x09, 0x7a,                    # ...Usage (Vendor Usage 0x7a)        404
        0x15, 0x01,                    # ...Logical Minimum (1)              406
        0x25, 0x03,                    # ...Logical Maximum (3)              408
        0x91, 0x00,                    # ...Output (Data,Arr,Abs)            410
        0xc0,                          # ..End Collection                    412
        0x09, 0x7c,                    # ..Usage (Vendor Usage 0x7c)         413
        0x15, 0x00,                    # ..Logical Minimum (0)               415
        0x26, 0xfe, 0x00,              # ..Logical Maximum (254)             417
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             420
        0xc0,                          # .End Collection                     422
        0x09, 0x92,                    # .Usage (Vendor Usage 0x92)          423
        0xa1, 0x02,                    # .Collection (Logical)               425
        0x85, 0x52,                    # ..Report ID (82)                    427
        0x09, 0x96,                    # ..Usage (Vendor Usage 0x96)         429
        0xa1, 0x02,                    # ..Collection (Logical)              431
        0x09, 0x9a,                    # ...Usage (Vendor Usage 0x9a)        433
        0x09, 0x99,                    # ...Usage (Vendor Usage 0x99)        435
        0x09, 0x97,                    # ...Usage (Vendor Usage 0x97)        437
        0x09, 0x98,                    # ...Usage (Vendor Usage 0x98)        439
        0x09, 0x9b,                    # ...Usage (Vendor Usage 0x9b)        441
        0x09, 0x9c,                    # ...Usage (Vendor Usage 0x9c)        443
        0x15, 0x01,                    # ...Logical Minimum (1)              445
        0x25, 0x06,                    # ...Logical Maximum (6)              447
        0x91, 0x00,                    # ...Output (Data,Arr,Abs)            449
        0xc0,                          # ..End Collection                    451
        0xc0,                          # .End Collection                     452
        0x05, 0xff,                    # .Usage Page (Vendor Usage Page 0xff) 453
        0x0a, 0x01, 0x03,              # .Usage (Vendor Usage 0x301)         455
        0xa1, 0x02,                    # .Collection (Logical)               458
        0x85, 0x40,                    # ..Report ID (64)                    460
        0x0a, 0x02, 0x03,              # ..Usage (Vendor Usage 0x302)        462
        0xa1, 0x02,                    # ..Collection (Logical)              465
        0x1a, 0x11, 0x03,              # ...Usage Minimum (785)              467
        0x2a, 0x20, 0x03,              # ...Usage Maximum (800)              470
        0x25, 0x10,                    # ...Logical Maximum (16)             473
        0x91, 0x00,                    # ...Output (Data,Arr,Abs)            475
        0xc0,                          # ..End Collection                    477
        0x0a, 0x03, 0x03,              # ..Usage (Vendor Usage 0x303)        478
        0x15, 0x00,                    # ..Logical Minimum (0)               481
        0x27, 0xff, 0xff, 0x00, 0x00,  # ..Logical Maximum (65535)           483
        0x75, 0x10,                    # ..Report Size (16)                  488
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             490
        0xc0,                          # .End Collection                     492
        0x05, 0x0f,                    # .Usage Page (Vendor Usage Page 0x0f) 493
        0x09, 0x7d,                    # .Usage (Vendor Usage 0x7d)          495
        0xa1, 0x02,                    # .Collection (Logical)               497
        0x85, 0x43,                    # ..Report ID (67)                    499
        0x09, 0x7e,                    # ..Usage (Vendor Usage 0x7e)         501
        0x26, 0x80, 0x00,              # ..Logical Maximum (128)             503
        0x46, 0x10, 0x27,              # ..Physical Maximum (10000)          506
        0x75, 0x08,                    # ..Report Size (8)                   509
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             511
        0xc0,                          # .End Collection                     513
        0x09, 0x7f,                    # .Usage (Vendor Usage 0x7f)          514
        0xa1, 0x02,                    # .Collection (Logical)               516
        0x85, 0x0b,                    # ..Report ID (11)                    518
        0x09, 0x80,                    # ..Usage (Vendor Usage 0x80)         520
        0x26, 0xff, 0x7f,              # ..Logical Maximum (32767)           522
        0x45, 0x00,                    # ..Physical Maximum (0)              525
        0x75, 0x0f,                    # ..Report Size (15)                  527
        0xb1, 0x03,                    # ..Feature (Cnst,Var,Abs)            529
        0x09, 0xa9,                    # ..Usage (Vendor Usage 0xa9)         531
        0x25, 0x01,                    # ..Logical Maximum (1)               533
        0x75, 0x01,                    # ..Report Size (1)                   535
        0xb1, 0x03,                    # ..Feature (Cnst,Var,Abs)            537
        0x09, 0x83,                    # ..Usage (Vendor Usage 0x83)         539
        0x26, 0xff, 0x00,              # ..Logical Maximum (255)             541
        0x75, 0x08,                    # ..Report Size (8)                   544
        0xb1, 0x03,                    # ..Feature (Cnst,Var,Abs)            546
        0xc0,                          # .End Collection                     548
        0x09, 0xab,                    # .Usage (Vendor Usage 0xab)          549
        0xa1, 0x03,                    # .Collection (Report)                551
        0x85, 0x15,                    # ..Report ID (21)                    553
        0x09, 0x25,                    # ..Usage (Vendor Usage 0x25)         555
        0xa1, 0x02,                    # ..Collection (Logical)              557
        0x09, 0x26,                    # ...Usage (Vendor Usage 0x26)        559
        0x09, 0x30,                    # ...Usage (Vendor Usage 0x30)        561
        0x09, 0x32,                    # ...Usage (Vendor Usage 0x32)        563
        0x09, 0x31,                    # ...Usage (Vendor Usage 0x31)        565
        0x09, 0x33,                    # ...Usage (Vendor Usage 0x33)        567
        0x09, 0x34,                    # ...Usage (Vendor Usage 0x34)        569
        0x15, 0x01,                    # ...Logical Minimum (1)              571
        0x25, 0x06,                    # ...Logical Maximum (6)              573
        0xb1, 0x00,                    # ...Feature (Data,Arr,Abs)           575
        0xc0,                          # ..End Collection                    577
        0xc0,                          # .End Collection                     578
        0x09, 0x89,                    # .Usage (Vendor Usage 0x89)          579
        0xa1, 0x03,                    # .Collection (Report)                581
        0x85, 0x16,                    # ..Report ID (22)                    583
        0x09, 0x8b,                    # ..Usage (Vendor Usage 0x8b)         585
        0xa1, 0x02,                    # ..Collection (Logical)              587
        0x09, 0x8c,                    # ...Usage (Vendor Usage 0x8c)        589
        0x09, 0x8d,                    # ...Usage (Vendor Usage 0x8d)        591
        0x09, 0x8e,                    # ...Usage (Vendor Usage 0x8e)        593
        0x25, 0x03,                    # ...Logical Maximum (3)              595
        0xb1, 0x00,                    # ...Feature (Data,Arr,Abs)           597
        0xc0,                          # ..End Collection                    599
        0x09, 0x22,                    # ..Usage (Vendor Usage 0x22)         600
        0x15, 0x00,                    # ..Logical Minimum (0)               602
        0x26, 0xfe, 0x00,              # ..Logical Maximum (254)             604
        0xb1, 0x02,                    # ..Feature (Data,Var,Abs)            607
        0xc0,                          # .End Collection                     609
        0x09, 0x90,                    # .Usage (Vendor Usage 0x90)          610
        0xa1, 0x03,                    # .Collection (Report)                612
        0x85, 0x50,                    # ..Report ID (80)                    614
        0x09, 0x22,                    # ..Usage (Vendor Usage 0x22)         616
        0x26, 0xff, 0x00,              # ..Logical Maximum (255)             618
        0x91, 0x02,                    # ..Output (Data,Var,Abs)             621
        0xc0,                          # .End Collection                     623
        0xc0,                          # End Collection                      624
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None):
        super().__init__(rdesc, name=name, input_info=(BusType.USB, 0x06A3, 0xFF0D))
        self.buttons = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)


class AsusGamepad(BaseGamepad):
    # fmt: off
    report_descriptor = [
        0x05, 0x01,                    # Usage Page (Generic Desktop)        0
        0x09, 0x05,                    # Usage (Game Pad)                    2
        0xa1, 0x01,                    # Collection (Application)            4
        0x85, 0x01,                    # .Report ID (1)                      6
        0x05, 0x09,                    # .Usage Page (Button)                8
        0x0a, 0x01, 0x00,              # .Usage (Vendor Usage 0x01)          10
        0x0a, 0x02, 0x00,              # .Usage (Vendor Usage 0x02)          13
        0x0a, 0x04, 0x00,              # .Usage (Vendor Usage 0x04)          16
        0x0a, 0x05, 0x00,              # .Usage (Vendor Usage 0x05)          19
        0x0a, 0x07, 0x00,              # .Usage (Vendor Usage 0x07)          22
        0x0a, 0x08, 0x00,              # .Usage (Vendor Usage 0x08)          25
        0x0a, 0x0e, 0x00,              # .Usage (Vendor Usage 0x0e)          28
        0x0a, 0x0f, 0x00,              # .Usage (Vendor Usage 0x0f)          31
        0x0a, 0x0d, 0x00,              # .Usage (Vendor Usage 0x0d)          34
        0x05, 0x0c,                    # .Usage Page (Consumer Devices)      37
        0x0a, 0x24, 0x02,              # .Usage (AC Back)                    39
        0x0a, 0x23, 0x02,              # .Usage (AC Home)                    42
        0x15, 0x00,                    # .Logical Minimum (0)                45
        0x25, 0x01,                    # .Logical Maximum (1)                47
        0x75, 0x01,                    # .Report Size (1)                    49
        0x95, 0x0b,                    # .Report Count (11)                  51
        0x81, 0x02,                    # .Input (Data,Var,Abs)               53
        0x75, 0x01,                    # .Report Size (1)                    55
        0x95, 0x01,                    # .Report Count (1)                   57
        0x81, 0x03,                    # .Input (Cnst,Var,Abs)               59
        0x05, 0x01,                    # .Usage Page (Generic Desktop)       61
        0x75, 0x04,                    # .Report Size (4)                    63
        0x95, 0x01,                    # .Report Count (1)                   65
        0x25, 0x07,                    # .Logical Maximum (7)                67
        0x46, 0x3b, 0x01,              # .Physical Maximum (315)             69
        0x66, 0x14, 0x00,              # .Unit (Degrees,EngRotation)         72
        0x09, 0x39,                    # .Usage (Hat switch)                 75
        0x81, 0x42,                    # .Input (Data,Var,Abs,Null)          77
        0x66, 0x00, 0x00,              # .Unit (None)                        79
        0x09, 0x01,                    # .Usage (Pointer)                    82
        0xa1, 0x00,                    # .Collection (Physical)              84
        0x09, 0x30,                    # ..Usage (X)                         86
        0x09, 0x31,                    # ..Usage (Y)                         88
        0x09, 0x32,                    # ..Usage (Z)                         90
        0x09, 0x35,                    # ..Usage (Rz)                        92
        0x05, 0x02,                    # ..Usage Page (Simulation Controls)  94
        0x09, 0xc5,                    # ..Usage (Brake)                     96
        0x09, 0xc4,                    # ..Usage (Accelerator)               98
        0x15, 0x00,                    # ..Logical Minimum (0)               100
        0x26, 0xff, 0x00,              # ..Logical Maximum (255)             102
        0x35, 0x00,                    # ..Physical Minimum (0)              105
        0x46, 0xff, 0x00,              # ..Physical Maximum (255)            107
        0x75, 0x08,                    # ..Report Size (8)                   110
        0x95, 0x06,                    # ..Report Count (6)                  112
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              114
        0xc0,                          # .End Collection                     116
        0x85, 0x02,                    # .Report ID (2)                      117
        0x05, 0x08,                    # .Usage Page (LEDs)                  119
        0x0a, 0x01, 0x00,              # .Usage (Num Lock)                   121
        0x0a, 0x02, 0x00,              # .Usage (Caps Lock)                  124
        0x0a, 0x03, 0x00,              # .Usage (Scroll Lock)                127
        0x0a, 0x04, 0x00,              # .Usage (Compose)                    130
        0x15, 0x00,                    # .Logical Minimum (0)                133
        0x25, 0x01,                    # .Logical Maximum (1)                135
        0x75, 0x01,                    # .Report Size (1)                    137
        0x95, 0x04,                    # .Report Count (4)                   139
        0x91, 0x02,                    # .Output (Data,Var,Abs)              141
        0x75, 0x04,                    # .Report Size (4)                    143
        0x95, 0x01,                    # .Report Count (1)                   145
        0x91, 0x03,                    # .Output (Cnst,Var,Abs)              147
        0xc0,                          # End Collection                      149
        0x05, 0x0c,                    # Usage Page (Consumer Devices)       150
        0x09, 0x01,                    # Usage (Consumer Control)            152
        0xa1, 0x01,                    # Collection (Application)            154
        0x85, 0x03,                    # .Report ID (3)                      156
        0x05, 0x01,                    # .Usage Page (Generic Desktop)       158
        0x09, 0x06,                    # .Usage (Keyboard)                   160
        0xa1, 0x02,                    # .Collection (Logical)               162
        0x05, 0x06,                    # ..Usage Page (Generic Device Controls) 164
        0x09, 0x20,                    # ..Usage (Battery Strength)          166
        0x15, 0x00,                    # ..Logical Minimum (0)               168
        0x26, 0xff, 0x00,              # ..Logical Maximum (255)             170
        0x75, 0x08,                    # ..Report Size (8)                   173
        0x95, 0x01,                    # ..Report Count (1)                  175
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              177
        0x06, 0xbc, 0xff,              # ..Usage Page (Vendor Usage Page 0xffbc) 179
        0x0a, 0xad, 0xbd,              # ..Usage (Vendor Usage 0xbdad)       182
        0x75, 0x08,                    # ..Report Size (8)                   185
        0x95, 0x06,                    # ..Report Count (6)                  187
        0x81, 0x02,                    # ..Input (Data,Var,Abs)              189
        0xc0,                          # .End Collection                     191
        0xc0,                          # End Collection                      192
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None):
        super().__init__(rdesc, name=name, input_info=(BusType.USB, 0x18D1, 0x2C40))
        self.buttons = (1, 2, 4, 5, 7, 8, 14, 15, 13)


class RaptorMach2Joystick(JoystickGamepad):
    axes_map = {
        "left_stick": {
            "x": AxisMapping("x"),
            "y": AxisMapping("y"),
        },
        "right_stick": {
            "x": AxisMapping("z"),
            "y": AxisMapping("Rz"),
        },
    }

    def __init__(
        self,
        name,
        rdesc=None,
        application="Joystick",
        input_info=(BusType.USB, 0x11C0, 0x5606),
    ):
        super().__init__(rdesc, application, name, input_info)
        self.buttons = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)
        self.hat_switch = 240  # null value is 240 as max is 239

    def event(
        self, *, left=(None, None), right=(None, None), hat_switch=None, buttons=None
    ):
        if hat_switch is not None:
            hat_switch *= 30

        return super().event(
            left=left, right=right, hat_switch=hat_switch, buttons=buttons
        )


class TestSaitekGamepad(BaseTest.TestGamepad):
    def create_device(self):
        return SaitekGamepad()


class TestAsusGamepad(BaseTest.TestGamepad):
    def create_device(self):
        return AsusGamepad()


class TestRaptorMach2Joystick(BaseTest.TestGamepad):
    hid_bpfs = [("FR-TEC__Raptor-Mach-2.bpf.o", True)]

    def create_device(self):
        return RaptorMach2Joystick(
            "uhid test Sanmos Group FR-TEC Raptor MACH 2",
            rdesc="05 01 09 04 a1 01 05 01 85 01 05 01 09 30 75 10 95 01 15 00 26 ff 07 46 ff 07 81 02 05 01 09 31 75 10 95 01 15 00 26 ff 07 46 ff 07 81 02 05 01 09 33 75 10 95 01 15 00 26 ff 03 46 ff 03 81 02 05 00 09 00 75 10 95 01 15 00 26 ff 03 46 ff 03 81 02 05 01 09 32 75 10 95 01 15 00 26 ff 03 46 ff 03 81 02 05 01 09 35 75 10 95 01 15 00 26 ff 03 46 ff 03 81 02 05 01 09 34 75 10 95 01 15 00 26 ff 07 46 ff 07 81 02 05 01 09 36 75 10 95 01 15 00 26 ff 03 46 ff 03 81 02 05 09 19 01 2a 1d 00 15 00 25 01 75 01 96 80 00 81 02 05 01 09 39 26 ef 00 46 68 01 65 14 75 10 95 01 81 42 05 01 09 00 75 08 95 1d 81 01 15 00 26 ef 00 85 58 26 ff 00 46 ff 00 75 08 95 3f 09 00 91 02 85 59 75 08 95 80 09 00 b1 02 c0",
            input_info=(BusType.USB, 0x11C0, 0x5606),
        )
