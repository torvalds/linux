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

from hidtools.device.base_gamepad import AsusGamepad, SaitekGamepad

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


class TestSaitekGamepad(BaseTest.TestGamepad):
    def create_device(self):
        return SaitekGamepad()


class TestAsusGamepad(BaseTest.TestGamepad):
    def create_device(self):
        return AsusGamepad()
