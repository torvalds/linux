#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2021 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2021 Red Hat, Inc.
#

from . import base
import copy
from enum import Enum
from hidtools.util import BusType
from .base import HidBpf
import libevdev
import logging
import pytest
from typing import Dict, List, Optional, Tuple

logger = logging.getLogger("hidtools.test.tablet")


class BtnTouch(Enum):
    """Represents whether the BTN_TOUCH event is set to True or False"""

    DOWN = True
    UP = False


class ToolType(Enum):
    PEN = libevdev.EV_KEY.BTN_TOOL_PEN
    RUBBER = libevdev.EV_KEY.BTN_TOOL_RUBBER


class BtnPressed(Enum):
    """Represents whether a button is pressed on the stylus"""

    PRIMARY_PRESSED = libevdev.EV_KEY.BTN_STYLUS
    SECONDARY_PRESSED = libevdev.EV_KEY.BTN_STYLUS2
    THIRD_PRESSED = libevdev.EV_KEY.BTN_STYLUS3


class PenState(Enum):
    """Pen states according to Microsoft reference:
    https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/windows-pen-states

    We extend it with the various buttons when we need to check them.
    """

    PEN_IS_OUT_OF_RANGE = BtnTouch.UP, None, False
    PEN_IS_IN_RANGE = BtnTouch.UP, ToolType.PEN, False
    PEN_IS_IN_RANGE_WITH_BUTTON = BtnTouch.UP, ToolType.PEN, True
    PEN_IS_IN_CONTACT = BtnTouch.DOWN, ToolType.PEN, False
    PEN_IS_IN_CONTACT_WITH_BUTTON = BtnTouch.DOWN, ToolType.PEN, True
    PEN_IS_IN_RANGE_WITH_ERASING_INTENT = BtnTouch.UP, ToolType.RUBBER, False
    PEN_IS_IN_RANGE_WITH_ERASING_INTENT_WITH_BUTTON = BtnTouch.UP, ToolType.RUBBER, True
    PEN_IS_ERASING = BtnTouch.DOWN, ToolType.RUBBER, False
    PEN_IS_ERASING_WITH_BUTTON = BtnTouch.DOWN, ToolType.RUBBER, True

    def __init__(
        self, touch: BtnTouch, tool: Optional[ToolType], button: Optional[bool]
    ):
        self.touch = touch  # type: ignore
        self.tool = tool  # type: ignore
        self.button = button  # type: ignore

    @classmethod
    def from_evdev(cls, evdev, test_button) -> "PenState":
        touch = BtnTouch(evdev.value[libevdev.EV_KEY.BTN_TOUCH])
        tool = None
        button = False
        if (
            evdev.value[libevdev.EV_KEY.BTN_TOOL_RUBBER]
            and not evdev.value[libevdev.EV_KEY.BTN_TOOL_PEN]
        ):
            tool = ToolType(libevdev.EV_KEY.BTN_TOOL_RUBBER)
        elif (
            evdev.value[libevdev.EV_KEY.BTN_TOOL_PEN]
            and not evdev.value[libevdev.EV_KEY.BTN_TOOL_RUBBER]
        ):
            tool = ToolType(libevdev.EV_KEY.BTN_TOOL_PEN)
        elif (
            evdev.value[libevdev.EV_KEY.BTN_TOOL_PEN]
            or evdev.value[libevdev.EV_KEY.BTN_TOOL_RUBBER]
        ):
            raise ValueError("2 tools are not allowed")

        # we take only the provided button into account
        if test_button is not None:
            button = bool(evdev.value[test_button.value])

        # the kernel tends to insert an EV_SYN once removing the tool, so
        # the button will be released after
        if tool is None:
            button = False

        return cls((touch, tool, button))  # type: ignore

    def apply(
        self, events: List[libevdev.InputEvent], strict: bool, test_button: BtnPressed
    ) -> "PenState":
        if libevdev.EV_SYN.SYN_REPORT in events:
            raise ValueError("EV_SYN is in the event sequence")
        touch = self.touch
        touch_found = False
        tool = self.tool
        tool_found = False
        button = self.button
        button_found = False

        for ev in events:
            if ev == libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH):
                if touch_found:
                    raise ValueError(f"duplicated BTN_TOUCH in {events}")
                touch_found = True
                touch = BtnTouch(ev.value)
            elif ev in (
                libevdev.InputEvent(libevdev.EV_KEY.BTN_TOOL_PEN),
                libevdev.InputEvent(libevdev.EV_KEY.BTN_TOOL_RUBBER),
            ):
                if tool_found:
                    raise ValueError(f"duplicated BTN_TOOL_* in {events}")
                tool_found = True
                tool = ToolType(ev.code) if ev.value else None
            elif test_button is not None and ev in (test_button.value,):
                if button_found:
                    raise ValueError(f"duplicated BTN_STYLUS* in {events}")
                button_found = True
                button = bool(ev.value)

        # the kernel tends to insert an EV_SYN once removing the tool, so
        # the button will be released after
        if tool is None:
            button = False

        new_state = PenState((touch, tool, button))  # type: ignore
        if strict:
            assert (
                new_state in self.valid_transitions()
            ), f"moving from {self} to {new_state} is forbidden"
        else:
            assert (
                new_state in self.historically_tolerated_transitions()
            ), f"moving from {self} to {new_state} is forbidden"

        return new_state

    def valid_transitions(self) -> Tuple["PenState", ...]:
        """Following the state machine in the URL above.

        Note that those transitions are from the evdev point of view, not HID"""
        if self == PenState.PEN_IS_OUT_OF_RANGE:
            return (
                PenState.PEN_IS_OUT_OF_RANGE,
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
                PenState.PEN_IS_ERASING,
            )

        if self == PenState.PEN_IS_IN_RANGE:
            return (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_OUT_OF_RANGE,
                PenState.PEN_IS_IN_CONTACT,
            )

        if self == PenState.PEN_IS_IN_CONTACT:
            return (
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE,
            )

        if self == PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT:
            return (
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_OUT_OF_RANGE,
                PenState.PEN_IS_ERASING,
            )

        if self == PenState.PEN_IS_ERASING:
            return (
                PenState.PEN_IS_ERASING,
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
            )

        if self == PenState.PEN_IS_IN_RANGE_WITH_BUTTON:
            return (
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_OUT_OF_RANGE,
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
            )

        if self == PenState.PEN_IS_IN_CONTACT_WITH_BUTTON:
            return (
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
            )

        return tuple()

    def historically_tolerated_transitions(self) -> Tuple["PenState", ...]:
        """Following the state machine in the URL above, with a couple of addition
        for skipping the in-range state, due to historical reasons.

        Note that those transitions are from the evdev point of view, not HID"""
        if self == PenState.PEN_IS_OUT_OF_RANGE:
            return (
                PenState.PEN_IS_OUT_OF_RANGE,
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
                PenState.PEN_IS_ERASING,
            )

        if self == PenState.PEN_IS_IN_RANGE:
            return (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_OUT_OF_RANGE,
                PenState.PEN_IS_IN_CONTACT,
            )

        if self == PenState.PEN_IS_IN_CONTACT:
            return (
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_OUT_OF_RANGE,
            )

        if self == PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT:
            return (
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_OUT_OF_RANGE,
                PenState.PEN_IS_ERASING,
            )

        if self == PenState.PEN_IS_ERASING:
            return (
                PenState.PEN_IS_ERASING,
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_OUT_OF_RANGE,
            )

        if self == PenState.PEN_IS_IN_RANGE_WITH_BUTTON:
            return (
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_OUT_OF_RANGE,
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
            )

        if self == PenState.PEN_IS_IN_CONTACT_WITH_BUTTON:
            return (
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_OUT_OF_RANGE,
            )

        return tuple()

    @staticmethod
    def legal_transitions() -> Dict[str, Tuple["PenState", ...]]:
        """This is the first half of the Windows Pen Implementation state machine:
        we don't have Invert nor Erase bits, so just move in/out-of-range or proximity.
        https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/windows-pen-states
        """
        return {
            "in-range": (PenState.PEN_IS_IN_RANGE,),
            "in-range -> out-of-range": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_OUT_OF_RANGE,
            ),
            "in-range -> touch": (PenState.PEN_IS_IN_RANGE, PenState.PEN_IS_IN_CONTACT),
            "in-range -> touch -> release": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_RANGE,
            ),
            "in-range -> touch -> release -> out-of-range": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_OUT_OF_RANGE,
            ),
        }

    @staticmethod
    def legal_transitions_with_invert() -> Dict[str, Tuple["PenState", ...]]:
        """This is the second half of the Windows Pen Implementation state machine:
        we now have Invert and Erase bits, so move in/out or proximity with the intend
        to erase.
        https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/windows-pen-states
        """
        return {
            "hover-erasing": (PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,),
            "hover-erasing -> out-of-range": (
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_OUT_OF_RANGE,
            ),
            "hover-erasing -> erase": (
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_ERASING,
            ),
            "hover-erasing -> erase -> release": (
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_ERASING,
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
            ),
            "hover-erasing -> erase -> release -> out-of-range": (
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_ERASING,
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_OUT_OF_RANGE,
            ),
            "hover-erasing -> in-range": (
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_IN_RANGE,
            ),
            "in-range -> hover-erasing": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
            ),
        }

    @staticmethod
    def legal_transitions_with_button() -> Dict[str, Tuple["PenState", ...]]:
        """We revisit the Windows Pen Implementation state machine:
        we now have a button.
        """
        return {
            "hover-button": (PenState.PEN_IS_IN_RANGE_WITH_BUTTON,),
            "hover-button -> out-of-range": (
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_OUT_OF_RANGE,
            ),
            "in-range -> button-press": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
            ),
            "in-range -> button-press -> button-release": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE,
            ),
            "in-range -> touch -> button-press -> button-release": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
                PenState.PEN_IS_IN_CONTACT,
            ),
            "in-range -> touch -> button-press -> release -> button-release": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE,
            ),
            "in-range -> button-press -> touch -> release -> button-release": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_IN_RANGE,
            ),
            "in-range -> button-press -> touch -> button-release -> release": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
                PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_RANGE,
            ),
        }

    @staticmethod
    def tolerated_transitions() -> Dict[str, Tuple["PenState", ...]]:
        """This is not adhering to the Windows Pen Implementation state machine
        but we should expect the kernel to behave properly, mostly for historical
        reasons."""
        return {
            "direct-in-contact": (PenState.PEN_IS_IN_CONTACT,),
            "direct-in-contact -> out-of-range": (
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_OUT_OF_RANGE,
            ),
        }

    @staticmethod
    def tolerated_transitions_with_invert() -> Dict[str, Tuple["PenState", ...]]:
        """This is the second half of the Windows Pen Implementation state machine:
        we now have Invert and Erase bits, so move in/out or proximity with the intend
        to erase.
        https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/windows-pen-states
        """
        return {
            "direct-erase": (PenState.PEN_IS_ERASING,),
            "direct-erase -> out-of-range": (
                PenState.PEN_IS_ERASING,
                PenState.PEN_IS_OUT_OF_RANGE,
            ),
        }

    @staticmethod
    def broken_transitions() -> Dict[str, Tuple["PenState", ...]]:
        """Those tests are definitely not part of the Windows specification.
        However, a half broken device might export those transitions.
        For example, a pen that has the eraser button might wobble between
        touching and erasing if the tablet doesn't enforce the Windows
        state machine."""
        return {
            "in-range -> touch -> erase -> hover-erase": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_ERASING,
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
            ),
            "in-range -> erase -> hover-erase": (
                PenState.PEN_IS_IN_RANGE,
                PenState.PEN_IS_ERASING,
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
            ),
            "hover-erase -> erase -> touch -> in-range": (
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_ERASING,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_RANGE,
            ),
            "hover-erase -> touch -> in-range": (
                PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_IN_RANGE,
            ),
            "touch -> erase -> touch -> erase": (
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_ERASING,
                PenState.PEN_IS_IN_CONTACT,
                PenState.PEN_IS_ERASING,
            ),
        }


class Pen(object):
    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.tipswitch = False
        self.tippressure = 15
        self.azimuth = 0
        self.inrange = False
        self.width = 10
        self.height = 10
        self.barrelswitch = False
        self.secondarybarrelswitch = False
        self.invert = False
        self.eraser = False
        self.xtilt = 1
        self.ytilt = 1
        self.twist = 1
        self._old_values = None
        self.current_state = None

    def restore(self):
        if self._old_values is not None:
            for i in [
                "x",
                "y",
                "tippressure",
                "azimuth",
                "width",
                "height",
                "twist",
                "xtilt",
                "ytilt",
            ]:
                setattr(self, i, getattr(self._old_values, i))

    def backup(self):
        self._old_values = copy.copy(self)

    def __assert_axis(self, evdev, axis, value):
        if (
            axis == libevdev.EV_KEY.BTN_TOOL_RUBBER
            and evdev.value[libevdev.EV_KEY.BTN_TOOL_RUBBER] is None
        ):
            return

        assert (
            evdev.value[axis] == value
        ), f"assert evdev.value[{axis}] ({evdev.value[axis]}) != {value}"

    def assert_expected_input_events(self, evdev, button):
        assert evdev.value[libevdev.EV_ABS.ABS_X] == self.x
        assert evdev.value[libevdev.EV_ABS.ABS_Y] == self.y

        # assert no other buttons than the tested ones are set
        buttons = [
            BtnPressed.PRIMARY_PRESSED,
            BtnPressed.SECONDARY_PRESSED,
            BtnPressed.THIRD_PRESSED,
        ]
        if button is not None:
            buttons.remove(button)
        for b in buttons:
            assert evdev.value[b.value] is None or evdev.value[b.value] == False

        assert self.current_state == PenState.from_evdev(evdev, button)


class PenDigitizer(base.UHIDTestDevice):
    def __init__(
        self,
        name,
        rdesc_str=None,
        rdesc=None,
        application="Pen",
        physical="Stylus",
        input_info=(BusType.USB, 1, 2),
        evdev_name_suffix=None,
    ):
        super().__init__(name, application, rdesc_str, rdesc, input_info)
        self.physical = physical
        self.cur_application = application
        if evdev_name_suffix is not None:
            self.name += evdev_name_suffix

        self.fields = []
        for r in self.parsed_rdesc.input_reports.values():
            if r.application_name == self.application:
                physicals = [f.physical_name for f in r]
                if self.physical not in physicals and None not in physicals:
                    continue
                self.fields = [f.usage_name for f in r]

    def move_to(self, pen, state, button):
        # fill in the previous values
        if pen.current_state == PenState.PEN_IS_OUT_OF_RANGE:
            pen.restore()

        print(f"\n  *** pen is moving to {state} ***")

        if state == PenState.PEN_IS_OUT_OF_RANGE:
            pen.backup()
            pen.x = 0
            pen.y = 0
            pen.tipswitch = False
            pen.tippressure = 0
            pen.azimuth = 0
            pen.inrange = False
            pen.width = 0
            pen.height = 0
            pen.invert = False
            pen.eraser = False
            pen.xtilt = 0
            pen.ytilt = 0
            pen.twist = 0
            pen.barrelswitch = False
            pen.secondarybarrelswitch = False
        elif state == PenState.PEN_IS_IN_RANGE:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = False
            pen.eraser = False
            pen.barrelswitch = False
            pen.secondarybarrelswitch = False
        elif state == PenState.PEN_IS_IN_CONTACT:
            pen.tipswitch = True
            pen.inrange = True
            pen.invert = False
            pen.eraser = False
            pen.barrelswitch = False
            pen.secondarybarrelswitch = False
        elif state == PenState.PEN_IS_IN_RANGE_WITH_BUTTON:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = False
            pen.eraser = False
            assert button is not None
            pen.barrelswitch = button == BtnPressed.PRIMARY_PRESSED
            pen.secondarybarrelswitch = button == BtnPressed.SECONDARY_PRESSED
        elif state == PenState.PEN_IS_IN_CONTACT_WITH_BUTTON:
            pen.tipswitch = True
            pen.inrange = True
            pen.invert = False
            pen.eraser = False
            assert button is not None
            pen.barrelswitch = button == BtnPressed.PRIMARY_PRESSED
            pen.secondarybarrelswitch = button == BtnPressed.SECONDARY_PRESSED
        elif state == PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = True
            pen.eraser = False
            pen.barrelswitch = False
            pen.secondarybarrelswitch = False
        elif state == PenState.PEN_IS_ERASING:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = False
            pen.eraser = True
            pen.barrelswitch = False
            pen.secondarybarrelswitch = False

        pen.current_state = state

    def event(self, pen, button):
        rs = []
        r = self.create_report(application=self.cur_application, data=pen)
        self.call_input_event(r)
        rs.append(r)
        return rs

    def get_report(self, req, rnum, rtype):
        if rtype != self.UHID_FEATURE_REPORT:
            return (1, [])

        rdesc = None
        for v in self.parsed_rdesc.feature_reports.values():
            if v.report_ID == rnum:
                rdesc = v

        if rdesc is None:
            return (1, [])

        return (1, [])

    def set_report(self, req, rnum, rtype, data):
        if rtype != self.UHID_FEATURE_REPORT:
            return 1

        rdesc = None
        for v in self.parsed_rdesc.feature_reports.values():
            if v.report_ID == rnum:
                rdesc = v

        if rdesc is None:
            return 1

        return 1


class BaseTest:
    class TestTablet(base.BaseTestCase.TestUhid):
        def create_device(self):
            raise Exception("please reimplement me in subclasses")

        def post(self, uhdev, pen, test_button):
            r = uhdev.event(pen, test_button)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            return events

        def validate_transitions(
            self, from_state, pen, evdev, events, allow_intermediate_states, button
        ):
            # check that the final state is correct
            pen.assert_expected_input_events(evdev, button)

            state = from_state

            # check that the transitions are valid
            sync_events = []
            while libevdev.InputEvent(libevdev.EV_SYN.SYN_REPORT) in events:
                # split the first EV_SYN from the list
                idx = events.index(libevdev.InputEvent(libevdev.EV_SYN.SYN_REPORT))
                sync_events = events[:idx]
                events = events[idx + 1 :]

                # now check for a valid transition
                state = state.apply(sync_events, not allow_intermediate_states, button)

            if events:
                state = state.apply(sync_events, not allow_intermediate_states, button)

        def _test_states(
            self, state_list, scribble, allow_intermediate_states, button=None
        ):
            """Internal method to test against a list of
            transition between states.
            state_list is a list of PenState objects
            scribble is a boolean which tells if we need
            to wobble a little the X,Y coordinates of the pen
            between each state transition."""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            cur_state = PenState.PEN_IS_OUT_OF_RANGE

            p = Pen(50, 60)
            uhdev.move_to(p, PenState.PEN_IS_OUT_OF_RANGE, button)
            events = self.post(uhdev, p, button)
            self.validate_transitions(
                cur_state, p, evdev, events, allow_intermediate_states, button
            )

            cur_state = p.current_state

            for state in state_list:
                if scribble and cur_state != PenState.PEN_IS_OUT_OF_RANGE:
                    p.x += 1
                    p.y -= 1
                    events = self.post(uhdev, p, button)
                    self.validate_transitions(
                        cur_state, p, evdev, events, allow_intermediate_states, button
                    )
                    assert len(events) >= 3  # X, Y, SYN
                uhdev.move_to(p, state, button)
                if scribble and state != PenState.PEN_IS_OUT_OF_RANGE:
                    p.x += 1
                    p.y -= 1
                events = self.post(uhdev, p, button)
                self.validate_transitions(
                    cur_state, p, evdev, events, allow_intermediate_states, button
                )
                cur_state = p.current_state

        @pytest.mark.parametrize("scribble", [True, False], ids=["scribble", "static"])
        @pytest.mark.parametrize(
            "state_list",
            [pytest.param(v, id=k) for k, v in PenState.legal_transitions().items()],
        )
        def test_valid_pen_states(self, state_list, scribble):
            """This is the first half of the Windows Pen Implementation state machine:
            we don't have Invert nor Erase bits, so just move in/out-of-range or proximity.
            https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/windows-pen-states
            """
            self._test_states(state_list, scribble, allow_intermediate_states=False)

        @pytest.mark.parametrize("scribble", [True, False], ids=["scribble", "static"])
        @pytest.mark.parametrize(
            "state_list",
            [
                pytest.param(v, id=k)
                for k, v in PenState.tolerated_transitions().items()
            ],
        )
        def test_tolerated_pen_states(self, state_list, scribble):
            """This is not adhering to the Windows Pen Implementation state machine
            but we should expect the kernel to behave properly, mostly for historical
            reasons."""
            self._test_states(state_list, scribble, allow_intermediate_states=True)

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: "Barrel Switch" not in uhdev.fields,
            "Device not compatible, missing Barrel Switch usage",
        )
        @pytest.mark.parametrize("scribble", [True, False], ids=["scribble", "static"])
        @pytest.mark.parametrize(
            "state_list",
            [
                pytest.param(v, id=k)
                for k, v in PenState.legal_transitions_with_button().items()
            ],
        )
        def test_valid_primary_button_pen_states(self, state_list, scribble):
            """Rework the transition state machine by adding the primary button."""
            self._test_states(
                state_list,
                scribble,
                allow_intermediate_states=False,
                button=BtnPressed.PRIMARY_PRESSED,
            )

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: "Secondary Barrel Switch" not in uhdev.fields,
            "Device not compatible, missing Secondary Barrel Switch usage",
        )
        @pytest.mark.parametrize("scribble", [True, False], ids=["scribble", "static"])
        @pytest.mark.parametrize(
            "state_list",
            [
                pytest.param(v, id=k)
                for k, v in PenState.legal_transitions_with_button().items()
            ],
        )
        def test_valid_secondary_button_pen_states(self, state_list, scribble):
            """Rework the transition state machine by adding the secondary button."""
            self._test_states(
                state_list,
                scribble,
                allow_intermediate_states=False,
                button=BtnPressed.SECONDARY_PRESSED,
            )

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: "Third Barrel Switch" not in uhdev.fields,
            "Device not compatible, missing Third Barrel Switch usage",
        )
        @pytest.mark.parametrize("scribble", [True, False], ids=["scribble", "static"])
        @pytest.mark.parametrize(
            "state_list",
            [
                pytest.param(v, id=k)
                for k, v in PenState.legal_transitions_with_button().items()
            ],
        )
        def test_valid_third_button_pen_states(self, state_list, scribble):
            """Rework the transition state machine by adding the secondary button."""
            self._test_states(
                state_list,
                scribble,
                allow_intermediate_states=False,
                button=BtnPressed.THIRD_PRESSED,
            )

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: "Invert" not in uhdev.fields,
            "Device not compatible, missing Invert usage",
        )
        @pytest.mark.parametrize("scribble", [True, False], ids=["scribble", "static"])
        @pytest.mark.parametrize(
            "state_list",
            [
                pytest.param(v, id=k)
                for k, v in PenState.legal_transitions_with_invert().items()
            ],
        )
        def test_valid_invert_pen_states(self, state_list, scribble):
            """This is the second half of the Windows Pen Implementation state machine:
            we now have Invert and Erase bits, so move in/out or proximity with the intend
            to erase.
            https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/windows-pen-states
            """
            self._test_states(state_list, scribble, allow_intermediate_states=False)

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: "Invert" not in uhdev.fields,
            "Device not compatible, missing Invert usage",
        )
        @pytest.mark.parametrize("scribble", [True, False], ids=["scribble", "static"])
        @pytest.mark.parametrize(
            "state_list",
            [
                pytest.param(v, id=k)
                for k, v in PenState.tolerated_transitions_with_invert().items()
            ],
        )
        def test_tolerated_invert_pen_states(self, state_list, scribble):
            """This is the second half of the Windows Pen Implementation state machine:
            we now have Invert and Erase bits, so move in/out or proximity with the intend
            to erase.
            https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/windows-pen-states
            """
            self._test_states(state_list, scribble, allow_intermediate_states=True)

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: "Invert" not in uhdev.fields,
            "Device not compatible, missing Invert usage",
        )
        @pytest.mark.parametrize("scribble", [True, False], ids=["scribble", "static"])
        @pytest.mark.parametrize(
            "state_list",
            [pytest.param(v, id=k) for k, v in PenState.broken_transitions().items()],
        )
        def test_tolerated_broken_pen_states(self, state_list, scribble):
            """Those tests are definitely not part of the Windows specification.
            However, a half broken device might export those transitions.
            For example, a pen that has the eraser button might wobble between
            touching and erasing if the tablet doesn't enforce the Windows
            state machine."""
            self._test_states(state_list, scribble, allow_intermediate_states=True)


class GXTP_pen(PenDigitizer):
    def event(self, pen, test_button):
        if not hasattr(self, "prev_tip_state"):
            self.prev_tip_state = False

        internal_pen = copy.copy(pen)

        # bug in the controller: when the pen touches the
        # surface, in-range stays to 1, but when
        # the pen moves in-range gets reverted to 0
        if pen.tipswitch and self.prev_tip_state:
            internal_pen.inrange = False

        self.prev_tip_state = pen.tipswitch

        # another bug in the controller: when the pen is
        # inverted, invert is set to 1, but as soon as
        # the pen touches the surface, eraser is correctly
        # set to 1 but invert is released
        if pen.eraser:
            internal_pen.invert = False

        return super().event(internal_pen, test_button)


class USIPen(PenDigitizer):
    pass


class XPPen_ArtistPro16Gen2_28bd_095b(PenDigitizer):
    """
    Pen with two buttons and a rubber end, but which reports
    the second button as an eraser
    """

    def __init__(
        self,
        name,
        rdesc_str=None,
        rdesc=None,
        application="Pen",
        physical="Stylus",
        input_info=(BusType.USB, 0x28BD, 0x095B),
        evdev_name_suffix=None,
    ):
        super().__init__(
            name, rdesc_str, rdesc, application, physical, input_info, evdev_name_suffix
        )
        self.fields.append("Secondary Barrel Switch")

    def move_to(self, pen, state, button):
        # fill in the previous values
        if pen.current_state == PenState.PEN_IS_OUT_OF_RANGE:
            pen.restore()

        print(f"\n  *** pen is moving to {state} ***")

        if state == PenState.PEN_IS_OUT_OF_RANGE:
            pen.backup()
            pen.x = 0
            pen.y = 0
            pen.tipswitch = False
            pen.tippressure = 0
            pen.azimuth = 0
            pen.inrange = False
            pen.width = 0
            pen.height = 0
            pen.invert = False
            pen.eraser = False
            pen.xtilt = 0
            pen.ytilt = 0
            pen.twist = 0
            pen.barrelswitch = False
        elif state == PenState.PEN_IS_IN_RANGE:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = False
            pen.eraser = False
            pen.barrelswitch = False
        elif state == PenState.PEN_IS_IN_CONTACT:
            pen.tipswitch = True
            pen.inrange = True
            pen.invert = False
            pen.eraser = False
            pen.barrelswitch = False
        elif state == PenState.PEN_IS_IN_RANGE_WITH_BUTTON:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = False
            assert button is not None
            pen.barrelswitch = button == BtnPressed.PRIMARY_PRESSED
            pen.eraser = button == BtnPressed.SECONDARY_PRESSED
        elif state == PenState.PEN_IS_IN_CONTACT_WITH_BUTTON:
            pen.tipswitch = True
            pen.inrange = True
            pen.invert = False
            assert button is not None
            pen.barrelswitch = button == BtnPressed.PRIMARY_PRESSED
            pen.eraser = button == BtnPressed.SECONDARY_PRESSED
        elif state == PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = True
            pen.eraser = False
            pen.barrelswitch = False
        elif state == PenState.PEN_IS_ERASING:
            pen.tipswitch = True
            pen.inrange = True
            pen.invert = True
            pen.eraser = False
            pen.barrelswitch = False

        pen.current_state = state

    def event(self, pen, test_button):
        import math

        pen_copy = copy.copy(pen)
        width = 13.567
        height = 8.480
        tip_height = 0.055677699
        hx = tip_height * (32767 / width)
        hy = tip_height * (32767 / height)
        if pen_copy.xtilt != 0:
            pen_copy.x += round(hx * math.sin(math.radians(pen_copy.xtilt)))
        if pen_copy.ytilt != 0:
            pen_copy.y += round(hy * math.sin(math.radians(pen_copy.ytilt)))

        return super().event(pen_copy, test_button)


class XPPen_Artist24_28bd_093a(PenDigitizer):
    """
    Pen that reports secondary barrel switch through eraser
    """

    def __init__(
        self,
        name,
        rdesc_str=None,
        rdesc=None,
        application="Pen",
        physical="Stylus",
        input_info=(BusType.USB, 0x28BD, 0x093A),
        evdev_name_suffix=None,
    ):
        super().__init__(
            name, rdesc_str, rdesc, application, physical, input_info, evdev_name_suffix
        )
        self.fields.append("Secondary Barrel Switch")
        self.previous_state = PenState.PEN_IS_OUT_OF_RANGE

    def move_to(self, pen, state, button, debug=True):
        # fill in the previous values
        if pen.current_state == PenState.PEN_IS_OUT_OF_RANGE:
            pen.restore()

        if debug:
            print(f"\n  *** pen is moving to {state} ***")

        if state == PenState.PEN_IS_OUT_OF_RANGE:
            pen.backup()
            pen.tipswitch = False
            pen.tippressure = 0
            pen.azimuth = 0
            pen.inrange = False
            pen.width = 0
            pen.height = 0
            pen.invert = False
            pen.eraser = False
            pen.xtilt = 0
            pen.ytilt = 0
            pen.twist = 0
            pen.barrelswitch = False
        elif state == PenState.PEN_IS_IN_RANGE:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = False
            pen.eraser = False
            pen.barrelswitch = False
        elif state == PenState.PEN_IS_IN_CONTACT:
            pen.tipswitch = True
            pen.inrange = True
            pen.invert = False
            pen.eraser = False
            pen.barrelswitch = False
        elif state == PenState.PEN_IS_IN_RANGE_WITH_BUTTON:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = False
            assert button is not None
            pen.barrelswitch = button == BtnPressed.PRIMARY_PRESSED
            pen.eraser = button == BtnPressed.SECONDARY_PRESSED
        elif state == PenState.PEN_IS_IN_CONTACT_WITH_BUTTON:
            pen.tipswitch = True
            pen.inrange = True
            pen.invert = False
            assert button is not None
            pen.barrelswitch = button == BtnPressed.PRIMARY_PRESSED
            pen.eraser = button == BtnPressed.SECONDARY_PRESSED

        pen.current_state = state

    def send_intermediate_state(self, pen, state, button):
        intermediate_pen = copy.copy(pen)
        self.move_to(intermediate_pen, state, button, debug=False)
        return super().event(intermediate_pen, button)

    def event(self, pen, button):
        rs = []

        # the pen reliably sends in-range events in a normal case (non emulation of eraser mode)
        if self.previous_state == PenState.PEN_IS_IN_CONTACT:
            if pen.current_state == PenState.PEN_IS_OUT_OF_RANGE:
                rs.extend(
                    self.send_intermediate_state(pen, PenState.PEN_IS_IN_RANGE, button)
                )

        if button == BtnPressed.SECONDARY_PRESSED:
            if self.previous_state == PenState.PEN_IS_IN_RANGE:
                if pen.current_state == PenState.PEN_IS_IN_RANGE_WITH_BUTTON:
                    rs.extend(
                        self.send_intermediate_state(
                            pen, PenState.PEN_IS_OUT_OF_RANGE, button
                        )
                    )

            if self.previous_state == PenState.PEN_IS_IN_RANGE_WITH_BUTTON:
                if pen.current_state == PenState.PEN_IS_IN_RANGE:
                    rs.extend(
                        self.send_intermediate_state(
                            pen, PenState.PEN_IS_OUT_OF_RANGE, button
                        )
                    )

            if self.previous_state == PenState.PEN_IS_IN_CONTACT:
                if pen.current_state == PenState.PEN_IS_IN_CONTACT_WITH_BUTTON:
                    rs.extend(
                        self.send_intermediate_state(
                            pen, PenState.PEN_IS_OUT_OF_RANGE, button
                        )
                    )
                    rs.extend(
                        self.send_intermediate_state(
                            pen, PenState.PEN_IS_IN_RANGE_WITH_BUTTON, button
                        )
                    )

            if self.previous_state == PenState.PEN_IS_IN_CONTACT_WITH_BUTTON:
                if pen.current_state == PenState.PEN_IS_IN_CONTACT:
                    rs.extend(
                        self.send_intermediate_state(
                            pen, PenState.PEN_IS_OUT_OF_RANGE, button
                        )
                    )
                    rs.extend(
                        self.send_intermediate_state(
                            pen, PenState.PEN_IS_IN_RANGE, button
                        )
                    )

        rs.extend(super().event(pen, button))
        self.previous_state = pen.current_state
        return rs


class Huion_Kamvas_Pro_19_256c_006b(PenDigitizer):
    """
    Pen that reports secondary barrel switch through secondary TipSwtich
    and 3rd button through Invert
    """

    def __init__(
        self,
        name,
        rdesc_str=None,
        rdesc=None,
        application="Stylus",
        physical=None,
        input_info=(BusType.USB, 0x256C, 0x006B),
        evdev_name_suffix=None,
    ):
        super().__init__(
            name, rdesc_str, rdesc, application, physical, input_info, evdev_name_suffix
        )
        self.fields.append("Secondary Barrel Switch")
        self.fields.append("Third Barrel Switch")
        self.previous_state = PenState.PEN_IS_OUT_OF_RANGE

    def move_to(self, pen, state, button, debug=True):
        # fill in the previous values
        if pen.current_state == PenState.PEN_IS_OUT_OF_RANGE:
            pen.restore()

        if debug:
            print(f"\n  *** pen is moving to {state} ***")

        if state == PenState.PEN_IS_OUT_OF_RANGE:
            pen.backup()
            pen.tipswitch = False
            pen.tippressure = 0
            pen.azimuth = 0
            pen.inrange = False
            pen.width = 0
            pen.height = 0
            pen.invert = False
            pen.eraser = False
            pen.xtilt = 0
            pen.ytilt = 0
            pen.twist = 0
            pen.barrelswitch = False
            pen.secondarytipswitch = False
        elif state == PenState.PEN_IS_IN_RANGE:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = False
            pen.eraser = False
            pen.barrelswitch = False
            pen.secondarytipswitch = False
        elif state == PenState.PEN_IS_IN_CONTACT:
            pen.tipswitch = True
            pen.inrange = True
            pen.invert = False
            pen.eraser = False
            pen.barrelswitch = False
            pen.secondarytipswitch = False
        elif state == PenState.PEN_IS_IN_RANGE_WITH_BUTTON:
            pen.tipswitch = False
            pen.inrange = True
            pen.eraser = False
            assert button is not None
            pen.barrelswitch = button == BtnPressed.PRIMARY_PRESSED
            pen.secondarytipswitch = button == BtnPressed.SECONDARY_PRESSED
            pen.invert = button == BtnPressed.THIRD_PRESSED
        elif state == PenState.PEN_IS_IN_CONTACT_WITH_BUTTON:
            pen.tipswitch = True
            pen.inrange = True
            pen.eraser = False
            assert button is not None
            pen.barrelswitch = button == BtnPressed.PRIMARY_PRESSED
            pen.secondarytipswitch = button == BtnPressed.SECONDARY_PRESSED
            pen.invert = button == BtnPressed.THIRD_PRESSED
        elif state == PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = True
            pen.eraser = False
            pen.barrelswitch = False
            pen.secondarytipswitch = False
        elif state == PenState.PEN_IS_ERASING:
            pen.tipswitch = False
            pen.inrange = True
            pen.invert = False
            pen.eraser = True
            pen.barrelswitch = False
            pen.secondarytipswitch = False

        pen.current_state = state

    def call_input_event(self, report):
        if report[0] == 0x0A:
            # ensures the original second Eraser usage is null
            report[1] &= 0xDF

            # ensures the original last bit is equal to bit 6 (In Range)
            if report[1] & 0x40:
                report[1] |= 0x80

        super().call_input_event(report)

    def send_intermediate_state(self, pen, state, test_button):
        intermediate_pen = copy.copy(pen)
        self.move_to(intermediate_pen, state, test_button, debug=False)
        return super().event(intermediate_pen, test_button)

    def event(self, pen, button):
        rs = []

        # it's not possible to go between eraser mode or not without
        # going out-of-prox: the eraser mode is activated by presenting
        # the tail of the pen
        if self.previous_state in (
            PenState.PEN_IS_IN_RANGE,
            PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
            PenState.PEN_IS_IN_CONTACT,
            PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
        ) and pen.current_state in (
            PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
            PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT_WITH_BUTTON,
            PenState.PEN_IS_ERASING,
            PenState.PEN_IS_ERASING_WITH_BUTTON,
        ):
            rs.extend(
                self.send_intermediate_state(pen, PenState.PEN_IS_OUT_OF_RANGE, button)
            )

        # same than above except from eraser to normal
        if self.previous_state in (
            PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT,
            PenState.PEN_IS_IN_RANGE_WITH_ERASING_INTENT_WITH_BUTTON,
            PenState.PEN_IS_ERASING,
            PenState.PEN_IS_ERASING_WITH_BUTTON,
        ) and pen.current_state in (
            PenState.PEN_IS_IN_RANGE,
            PenState.PEN_IS_IN_RANGE_WITH_BUTTON,
            PenState.PEN_IS_IN_CONTACT,
            PenState.PEN_IS_IN_CONTACT_WITH_BUTTON,
        ):
            rs.extend(
                self.send_intermediate_state(pen, PenState.PEN_IS_OUT_OF_RANGE, button)
            )

        if self.previous_state == PenState.PEN_IS_OUT_OF_RANGE:
            if pen.current_state == PenState.PEN_IS_IN_RANGE_WITH_BUTTON:
                rs.extend(
                    self.send_intermediate_state(pen, PenState.PEN_IS_IN_RANGE, button)
                )

        rs.extend(super().event(pen, button))
        self.previous_state = pen.current_state
        return rs


################################################################################
#
# Windows 7 compatible devices
#
################################################################################
# class TestEgalax_capacitive_0eef_7224(BaseTest.TestTablet):
#     def create_device(self):
#         return PenDigitizer('uhid test egalax-capacitive_0eef_7224',
#                             rdesc='05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 34 49 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 37 29 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 34 49 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 37 29 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0',
#                             input_info=(BusType.USB, 0x0eef, 0x7224),
#                             evdev_name_suffix=' Touchscreen')
#
#
# class TestEgalax_capacitive_0eef_72fa(BaseTest.TestTablet):
#     def create_device(self):
#         return PenDigitizer('uhid test egalax-capacitive_0eef_72fa',
#                             rdesc='05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 72 22 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 87 13 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 72 22 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 87 13 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0',
#                             input_info=(BusType.USB, 0x0eef, 0x72fa),
#                             evdev_name_suffix=' Touchscreen')
#
#
# class TestEgalax_capacitive_0eef_7336(BaseTest.TestTablet):
#     def create_device(self):
#         return PenDigitizer('uhid test egalax-capacitive_0eef_7336',
#                             rdesc='05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 c1 20 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 c2 18 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 c1 20 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 c2 18 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0',
#                             input_info=(BusType.USB, 0x0eef, 0x7336),
#                             evdev_name_suffix=' Touchscreen')
#
#
# class TestEgalax_capacitive_0eef_7337(BaseTest.TestTablet):
#     def create_device(self):
#         return PenDigitizer('uhid test egalax-capacitive_0eef_7337',
#                             rdesc='05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 ae 17 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 c3 0e 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 ae 17 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 c3 0e 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0',
#                             input_info=(BusType.USB, 0x0eef, 0x7337),
#                             evdev_name_suffix=' Touchscreen')
#
#
# class TestEgalax_capacitive_0eef_7349(BaseTest.TestTablet):
#     def create_device(self):
#         return PenDigitizer('uhid test egalax-capacitive_0eef_7349',
#                             rdesc='05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 34 49 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 37 29 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 34 49 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 37 29 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0',
#                             input_info=(BusType.USB, 0x0eef, 0x7349),
#                             evdev_name_suffix=' Touchscreen')
#
#
# class TestEgalax_capacitive_0eef_73f4(BaseTest.TestTablet):
#     def create_device(self):
#         return PenDigitizer('uhid test egalax-capacitive_0eef_73f4',
#                             rdesc='05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 96 4e 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 23 2c 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 96 4e 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 23 2c 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0',
#                             input_info=(BusType.USB, 0x0eef, 0x73f4),
#                             evdev_name_suffix=' Touchscreen')
#
#  bogus: BTN_TOOL_PEN is not emitted
# class TestIrtouch_6615_0070(BaseTest.TestTablet):
#     def create_device(self):
#         return PenDigitizer('uhid test irtouch_6615_0070',
#                             rdesc='05 01 09 02 a1 01 85 10 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 06 81 03 05 01 09 30 09 31 15 00 26 ff 7f 75 10 95 02 81 02 c0 c0 05 0d 09 04 a1 01 85 30 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 09 30 26 ff 7f 55 0f 65 11 35 00 46 51 02 75 10 95 01 81 02 09 31 35 00 46 73 01 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 09 30 26 ff 7f 55 0f 65 11 35 00 46 51 02 75 10 95 01 81 02 09 31 35 00 46 73 01 81 02 c0 05 0d 09 54 15 00 26 02 00 75 08 95 01 81 02 85 03 09 55 15 00 26 ff 00 75 08 95 01 b1 02 c0 05 0d 09 0e a1 01 85 02 09 52 09 53 15 00 26 ff 00 75 08 95 02 b1 02 c0 05 0d 09 02 a1 01 85 20 09 20 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 05 01 09 30 26 ff 7f 55 0f 65 11 35 00 46 51 02 75 10 95 01 81 02 09 31 35 00 46 73 01 81 02 85 01 06 00 ff 09 01 75 08 95 01 b1 02 c0 c0',
#                             input_info=(BusType.USB, 0x6615, 0x0070))


class TestNexio_1870_0100(BaseTest.TestTablet):
    def create_device(self):
        return PenDigitizer(
            "uhid test nexio_1870_0100",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 05 0d 09 54 95 01 75 08 25 02 81 02 85 02 09 55 25 02 b1 02 c0 09 0e a1 01 85 03 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 04 05 09 95 03 75 01 19 01 29 03 15 00 25 01 81 02 95 01 75 05 81 01 05 01 75 10 95 02 09 30 09 31 15 00 26 ff 7f 81 02 c0 c0 05 0d 09 02 a1 01 85 05 09 20 a1 00 09 42 09 32 15 00 25 01 75 01 95 02 81 02 95 0e 81 03 05 01 26 ff 3f 75 10 95 01 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 c0 06 00 ff 09 01 a1 01 85 06 19 01 29 40 15 00 26 ff 00 75 08 95 40 81 00 19 01 29 40 91 00 c0",
            input_info=(BusType.USB, 0x1870, 0x0100),
        )


class TestNexio_1870_010d(BaseTest.TestTablet):
    def create_device(self):
        return PenDigitizer(
            "uhid test nexio_1870_010d",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 05 0d 09 54 95 01 75 08 25 02 81 02 85 02 09 55 25 06 b1 02 c0 09 0e a1 01 85 03 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 04 05 09 95 03 75 01 19 01 29 03 15 00 25 01 81 02 95 01 75 05 81 01 05 01 75 10 95 02 09 30 09 31 15 00 26 ff 7f 81 02 c0 c0 05 0d 09 02 a1 01 85 05 09 20 a1 00 09 42 09 32 15 00 25 01 75 01 95 02 81 02 95 0e 81 03 05 01 26 ff 3f 75 10 95 01 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 c0 06 00 ff 09 01 a1 01 85 06 19 01 29 40 15 00 26 ff 00 75 08 95 3e 81 00 19 01 29 40 91 00 c0",
            input_info=(BusType.USB, 0x1870, 0x010D),
        )


class TestNexio_1870_0119(BaseTest.TestTablet):
    def create_device(self):
        return PenDigitizer(
            "uhid test nexio_1870_0119",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 05 0d 09 54 95 01 75 08 25 02 81 02 85 02 09 55 25 06 b1 02 c0 09 0e a1 01 85 03 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 04 05 09 95 03 75 01 19 01 29 03 15 00 25 01 81 02 95 01 75 05 81 01 05 01 75 10 95 02 09 30 09 31 15 00 26 ff 7f 81 02 c0 c0 05 0d 09 02 a1 01 85 05 09 20 a1 00 09 42 09 32 15 00 25 01 75 01 95 02 81 02 95 0e 81 03 05 01 26 ff 3f 75 10 95 01 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 c0 06 00 ff 09 01 a1 01 85 06 19 01 29 40 15 00 26 ff 00 75 08 95 3e 81 00 19 01 29 40 91 00 c0",
            input_info=(BusType.USB, 0x1870, 0x0119),
        )


################################################################################
#
# Windows 8 compatible devices
#
################################################################################

# bogus: application is 'undefined'
# class Testatmel_03eb_8409(BaseTest.TestTablet):
#     def create_device(self):
#         return PenDigitizer('uhid test atmel_03eb_8409', rdesc='05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 35 00 35 00 46 18 06 26 77 0f 09 31 81 02 35 00 35 00 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 48 81 02 09 49 81 02 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 35 00 35 00 46 18 06 26 77 0f 09 31 81 02 35 00 35 00 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 48 81 02 09 49 81 02 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 35 00 35 00 46 18 06 26 77 0f 09 31 81 02 35 00 35 00 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 48 81 02 09 49 81 02 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 35 00 35 00 46 18 06 26 77 0f 09 31 81 02 35 00 35 00 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 48 81 02 09 49 81 02 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 35 00 35 00 46 18 06 26 77 0f 09 31 81 02 35 00 35 00 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 48 81 02 09 49 81 02 c0 05 0d 27 ff ff 00 00 75 10 95 01 09 56 81 02 15 00 25 1f 75 05 09 54 95 01 81 02 75 03 25 01 95 01 81 03 75 08 85 02 09 55 25 10 b1 02 06 00 ff 85 05 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 00 a1 01 85 03 09 20 a1 00 15 00 25 01 75 01 95 01 09 42 81 02 09 44 81 02 09 45 81 02 81 03 09 32 81 02 95 03 81 03 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 46 18 06 26 77 0f 09 31 81 02 05 0d 09 30 15 01 26 ff 00 75 08 95 01 81 02 c0 c0')


class Testatmel_03eb_840b(BaseTest.TestTablet):
    def create_device(self):
        return PenDigitizer(
            "uhid test atmel_03eb_840b",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 01 46 00 0a 26 ff 0f 09 30 81 02 09 00 81 03 46 a0 05 26 ff 0f 09 31 81 02 09 00 81 03 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 00 81 03 09 00 81 03 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 01 46 00 0a 26 ff 0f 09 30 81 02 09 00 81 03 46 a0 05 26 ff 0f 09 31 81 02 09 00 81 03 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 00 81 03 09 00 81 03 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 01 46 00 0a 26 ff 0f 09 30 81 02 09 00 81 03 46 a0 05 26 ff 0f 09 31 81 02 09 00 81 03 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 00 81 03 09 00 81 03 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 01 46 00 0a 26 ff 0f 09 30 81 02 09 00 81 03 46 a0 05 26 ff 0f 09 31 81 02 09 00 81 03 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 00 81 03 09 00 81 03 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 01 46 00 0a 26 ff 0f 09 30 81 02 09 00 81 03 46 a0 05 26 ff 0f 09 31 81 02 09 00 81 03 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 00 81 03 09 00 81 03 c0 05 0d 27 ff ff 00 00 75 10 95 01 09 56 81 02 15 00 25 1f 75 05 09 54 95 01 81 02 75 03 25 01 95 01 81 03 75 08 85 02 09 55 25 10 b1 02 06 00 ff 85 05 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 02 a1 01 85 03 09 20 a1 00 15 00 25 01 75 01 95 01 09 42 81 02 09 44 81 02 09 45 81 02 81 03 09 32 81 02 95 03 81 03 05 01 55 0e 65 11 35 00 75 10 95 02 46 00 0a 26 ff 0f 09 30 81 02 46 a0 05 26 ff 0f 09 31 81 02 05 0d 09 30 15 01 26 ff 00 75 08 95 01 81 02 c0 c0",
        )


class Testn_trig_1b96_0c01(BaseTest.TestTablet):
    def create_device(self):
        return PenDigitizer(
            "uhid test n_trig_1b96_0c01",
            rdesc="75 08 15 00 26 ff 00 06 0b ff 09 0b a1 01 95 0f 09 29 85 29 b1 02 95 1f 09 2a 85 2a b1 02 95 3e 09 2b 85 2b b1 02 95 fe 09 2c 85 2c b1 02 96 fe 01 09 2d 85 2d b1 02 95 02 09 48 85 48 b1 02 95 0f 09 2e 85 2e 81 02 95 1f 09 2f 85 2f 81 02 95 3e 09 30 85 30 81 02 95 fe 09 31 85 31 81 02 96 fe 01 09 32 85 32 81 02 75 08 96 fe 0f 09 35 85 35 81 02 c0 05 0d 09 02 a1 01 85 01 09 20 35 00 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 b4 05 0d 09 30 26 00 01 81 02 06 00 ff 09 01 81 02 c0 85 0c 06 00 ff 09 0c 75 08 95 06 26 ff 00 b1 02 85 0b 09 0b 95 02 b1 02 85 11 09 11 b1 02 85 15 09 15 95 05 b1 02 85 18 09 18 95 0c b1 02 c0 05 0d 09 04 a1 01 85 03 06 00 ff 09 01 75 10 95 01 15 00 27 ff ff 00 00 81 02 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 56 75 20 95 01 27 ff ff ff 0f 81 02 85 04 09 55 75 08 95 01 25 0b b1 02 85 0a 06 00 ff 09 03 15 00 b1 02 85 1b 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0",
        )


class Testn_trig_1b96_0c03(BaseTest.TestTablet):
    def create_device(self):
        return PenDigitizer(
            "uhid test n_trig_1b96_0c03",
            rdesc="75 08 15 00 26 ff 00 06 0b ff 09 0b a1 01 95 0f 09 29 85 29 b1 02 95 1f 09 2a 85 2a b1 02 95 3e 09 2b 85 2b b1 02 95 fe 09 2c 85 2c b1 02 96 fe 01 09 2d 85 2d b1 02 95 02 09 48 85 48 b1 02 95 0f 09 2e 85 2e 81 02 95 1f 09 2f 85 2f 81 02 95 3e 09 30 85 30 81 02 95 fe 09 31 85 31 81 02 96 fe 01 09 32 85 32 81 02 75 08 96 fe 0f 09 35 85 35 81 02 c0 05 0d 09 02 a1 01 85 01 09 20 35 00 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 b4 05 0d 09 30 26 00 01 81 02 06 00 ff 09 01 81 02 c0 85 0c 06 00 ff 09 0c 75 08 95 06 26 ff 00 b1 02 85 0b 09 0b 95 02 b1 02 85 11 09 11 b1 02 85 15 09 15 95 05 b1 02 85 18 09 18 95 0c b1 02 c0 05 0d 09 04 a1 01 85 03 06 00 ff 09 01 75 10 95 01 15 00 27 ff ff 00 00 81 02 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 56 75 20 95 01 27 ff ff ff 0f 81 02 85 04 09 55 75 08 95 01 25 0b b1 02 85 0a 06 00 ff 09 03 15 00 b1 02 85 1b 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0",
        )


class Testn_trig_1b96_0f00(BaseTest.TestTablet):
    def create_device(self):
        return PenDigitizer(
            "uhid test n_trig_1b96_0f00",
            rdesc="75 08 15 00 26 ff 00 06 0b ff 09 0b a1 01 95 0f 09 29 85 29 b1 02 95 1f 09 2a 85 2a b1 02 95 3e 09 2b 85 2b b1 02 95 fe 09 2c 85 2c b1 02 96 fe 01 09 2d 85 2d b1 02 95 02 09 48 85 48 b1 02 95 0f 09 2e 85 2e 81 02 95 1f 09 2f 85 2f 81 02 95 3e 09 30 85 30 81 02 95 fe 09 31 85 31 81 02 96 fe 01 09 32 85 32 81 02 75 08 96 fe 0f 09 35 85 35 81 02 c0 05 0d 09 02 a1 01 85 01 09 20 35 00 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 b4 05 0d 09 30 26 00 01 81 02 06 00 ff 09 01 81 02 c0 85 0c 06 00 ff 09 0c 75 08 95 06 26 ff 00 b1 02 85 0b 09 0b 95 02 b1 02 85 11 09 11 b1 02 85 15 09 15 95 05 b1 02 85 18 09 18 95 0c b1 02 c0 05 0d 09 04 a1 01 85 03 06 00 ff 09 01 75 10 95 01 15 00 27 ff ff 00 00 81 02 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 56 75 20 95 01 27 ff ff ff 0f 81 02 85 04 09 55 75 08 95 01 25 0b b1 02 85 0a 06 00 ff 09 03 15 00 b1 02 85 1b 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0",
        )


class Testn_trig_1b96_0f04(BaseTest.TestTablet):
    def create_device(self):
        return PenDigitizer(
            "uhid test n_trig_1b96_0f04",
            rdesc="75 08 15 00 26 ff 00 06 0b ff 09 0b a1 01 95 0f 09 29 85 29 b1 02 95 1f 09 2a 85 2a b1 02 95 3e 09 2b 85 2b b1 02 95 fe 09 2c 85 2c b1 02 96 fe 01 09 2d 85 2d b1 02 95 02 09 48 85 48 b1 02 95 0f 09 2e 85 2e 81 02 95 1f 09 2f 85 2f 81 02 95 3e 09 30 85 30 81 02 95 fe 09 31 85 31 81 02 96 fe 01 09 32 85 32 81 02 75 08 96 fe 0f 09 35 85 35 81 02 c0 05 0d 09 02 a1 01 85 01 09 20 35 00 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0e 65 11 46 7f 0b 26 80 25 81 02 09 31 46 78 06 26 20 1c 81 02 b4 05 0d 09 30 26 00 01 81 02 06 00 ff 09 01 81 02 c0 85 0c 06 00 ff 09 0c 75 08 95 06 26 ff 00 b1 02 85 0b 09 0b 95 02 b1 02 85 11 09 11 b1 02 85 15 09 15 95 05 b1 02 85 18 09 18 95 0c b1 02 c0 05 0d 09 04 a1 01 85 03 06 00 ff 09 01 75 10 95 01 15 00 27 ff ff 00 00 81 02 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 7f 0b 26 80 25 81 02 09 31 46 78 06 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 7f 0b 26 80 25 81 02 09 31 46 78 06 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 56 75 20 95 01 27 ff ff ff 0f 81 02 85 04 09 55 75 08 95 01 25 0b b1 02 85 0a 06 00 ff 09 03 15 00 b1 02 85 1b 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0",
        )


class Testn_trig_1b96_1000(BaseTest.TestTablet):
    def create_device(self):
        return PenDigitizer(
            "uhid test n_trig_1b96_1000",
            rdesc="75 08 15 00 26 ff 00 06 0b ff 09 0b a1 01 95 0f 09 29 85 29 b1 02 95 1f 09 2a 85 2a b1 02 95 3e 09 2b 85 2b b1 02 95 fe 09 2c 85 2c b1 02 96 fe 01 09 2d 85 2d b1 02 95 02 09 48 85 48 b1 02 95 0f 09 2e 85 2e 81 02 95 1f 09 2f 85 2f 81 02 95 3e 09 30 85 30 81 02 95 fe 09 31 85 31 81 02 96 fe 01 09 32 85 32 81 02 75 08 96 fe 0f 09 35 85 35 81 02 c0 05 0d 09 02 a1 01 85 01 09 20 35 00 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 b4 05 0d 09 30 26 00 01 81 02 06 00 ff 09 01 81 02 c0 85 0c 06 00 ff 09 0c 75 08 95 06 26 ff 00 b1 02 85 0b 09 0b 95 02 b1 02 85 11 09 11 b1 02 85 15 09 15 95 05 b1 02 85 18 09 18 95 0c b1 02 c0 05 0d 09 04 a1 01 85 03 06 00 ff 09 01 75 10 95 01 15 00 27 ff ff 00 00 81 02 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 56 75 20 95 01 27 ff ff ff 0f 81 02 85 04 09 55 75 08 95 01 25 0b b1 02 85 0a 06 00 ff 09 03 15 00 b1 02 85 1b 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0",
        )


class TestGXTP_27c6_0113(BaseTest.TestTablet):
    def create_device(self):
        return GXTP_pen(
            "uhid test GXTP_27c6_0113",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 55 0e 65 11 35 00 15 00 09 42 25 01 75 01 95 01 81 02 95 07 81 01 95 01 75 08 09 51 81 02 75 10 05 01 26 00 14 46 1f 07 09 30 81 02 26 80 0c 46 77 04 09 31 81 02 05 0d c0 09 22 a1 02 09 42 25 01 75 01 95 01 81 02 95 07 81 01 95 01 75 08 09 51 81 02 75 10 05 01 26 00 14 46 1f 07 09 30 81 02 26 80 0c 46 77 04 09 31 81 02 05 0d c0 09 22 a1 02 09 42 25 01 75 01 95 01 81 02 95 07 81 01 95 01 75 08 09 51 81 02 75 10 05 01 26 00 14 46 1f 07 09 30 81 02 26 80 0c 46 77 04 09 31 81 02 05 0d c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 01 75 08 09 51 95 01 81 02 05 01 26 00 14 75 10 55 0e 65 11 09 30 35 00 46 1f 07 81 02 26 80 0c 46 77 04 09 31 81 02 05 0d c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 01 75 08 09 51 95 01 81 02 05 01 26 00 14 75 10 55 0e 65 11 09 30 35 00 46 1f 07 81 02 26 80 0c 46 77 04 09 31 81 02 05 0d c0 09 54 15 00 25 7f 75 08 95 01 81 02 85 02 09 55 95 01 25 0a b1 02 85 03 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 02 a1 01 85 08 09 20 a1 00 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 04 81 02 95 01 81 03 09 32 81 02 95 02 81 03 95 01 75 08 09 51 81 02 05 01 09 30 75 10 95 01 a4 55 0e 65 11 35 00 26 00 14 46 1f 07 81 42 09 31 26 80 0c 46 77 04 81 42 b4 05 0d 09 30 26 ff 0f 81 02 09 3d 65 14 55 0e 36 d8 dc 46 28 23 16 d8 dc 26 28 23 81 02 09 3e 81 02 c0 c0 06 f0 ff 09 01 a1 01 85 0e 09 01 15 00 25 ff 75 08 95 40 91 02 09 01 15 00 25 ff 75 08 95 40 81 02 c0 05 01 09 06 a1 01 85 04 05 07 09 e3 15 00 25 01 75 01 95 01 81 02 95 07 81 03 c0",
        )


################################################################################
#
# Windows 8 compatible devices with USI Pen
#
################################################################################


class TestElan_04f3_2A49(BaseTest.TestTablet):
    def create_device(self):
        return USIPen(
            "uhid test Elan_04f3_2A49",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 55 0f 65 11 35 00 45 ff 09 48 81 02 09 49 81 02 09 30 81 02 95 01 05 01 a4 26 cf 0f 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 01 81 02 26 77 0a 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 55 0f 65 11 35 00 45 ff 09 48 81 02 09 49 81 02 09 30 81 02 95 01 05 01 a4 26 cf 0f 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 01 81 02 26 77 0a 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 55 0f 65 11 35 00 45 ff 09 48 81 02 09 49 81 02 09 30 81 02 95 01 05 01 a4 26 cf 0f 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 01 81 02 26 77 0a 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 55 0f 65 11 35 00 45 ff 09 48 81 02 09 49 81 02 09 30 81 02 95 01 05 01 a4 26 cf 0f 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 01 81 02 26 77 0a 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 55 0f 65 11 35 00 45 ff 09 48 81 02 09 49 81 02 09 30 81 02 95 01 05 01 a4 26 cf 0f 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 01 81 02 26 77 0a 46 a6 00 09 31 81 02 b4 c0 05 0d 09 54 25 7f 96 01 00 75 08 81 02 85 0a 09 55 25 0a b1 02 85 44 06 00 ff 09 c5 16 00 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 ff 01 09 01 a1 01 85 02 16 00 00 26 ff 00 75 08 95 40 09 00 81 02 c0 06 00 ff 09 01 a1 01 85 03 75 08 95 20 09 01 91 02 c0 06 00 ff 09 01 a1 01 85 06 09 03 75 08 95 12 91 02 09 04 75 08 95 03 b1 02 c0 06 01 ff 09 01 a1 01 85 04 15 00 26 ff 00 75 08 95 13 09 00 81 02 c0 05 0d 09 02 a1 01 85 07 35 00 09 20 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0f 65 11 46 26 01 26 1c 48 81 42 09 31 46 a6 00 26 bc 2f 81 42 b4 05 0d 09 30 26 00 10 81 02 75 08 95 01 09 3b 25 64 81 42 09 38 15 00 25 02 81 02 09 5c 26 ff 00 81 02 09 5e 81 02 09 70 a1 02 15 01 25 06 09 72 09 73 09 74 09 75 09 76 09 77 81 20 09 5b 25 ff 75 40 81 02 c0 06 00 ff 75 08 95 02 09 01 81 02 c0 05 0d 85 60 09 81 a1 02 09 38 75 08 95 01 15 00 25 02 81 02 09 81 15 01 25 04 09 82 09 83 09 84 09 85 81 20 c0 85 61 09 5c a1 02 15 00 26 ff 00 75 08 95 01 09 38 b1 02 09 5c 26 ff 00 b1 02 09 5d 75 01 95 01 25 01 b1 02 95 07 b1 03 c0 85 62 09 5e a1 02 09 38 15 00 25 02 75 08 95 01 b1 02 09 5e 26 ff 00 b1 02 09 5f 75 01 25 01 b1 02 75 07 b1 03 c0 85 63 09 70 a1 02 75 08 95 01 15 00 25 02 09 38 b1 02 09 70 a1 02 25 06 09 72 09 73 09 74 09 75 09 76 09 77 b1 20 c0 09 71 75 01 25 01 b1 02 75 07 b1 03 c0 85 64 09 80 15 00 25 ff 75 40 95 01 b1 02 85 65 09 44 a1 02 09 38 75 08 95 01 25 02 b1 02 15 01 25 03 09 44 a1 02 09 a4 09 44 09 5a 09 45 09 a3 b1 20 c0 09 5a a1 02 09 a4 09 44 09 5a 09 45 09 a3 b1 20 c0 09 45 a1 02 09 a4 09 44 09 5a 09 45 09 a3 b1 20 c0 c0 85 66 75 08 95 01 05 0d 09 90 a1 02 09 38 25 02 b1 02 09 91 75 10 26 ff 0f b1 02 09 92 75 40 25 ff b1 02 05 06 09 2a 75 08 26 ff 00 a1 02 09 2d b1 02 09 2e b1 02 c0 c0 85 67 05 06 09 2b a1 02 05 0d 25 02 09 38 b1 02 05 06 09 2b a1 02 09 2d 26 ff 00 b1 02 09 2e b1 02 c0 c0 85 68 06 00 ff 09 01 a1 02 05 0d 09 38 75 08 95 01 25 02 b1 02 06 00 ff 09 01 75 10 27 ff ff 00 00 b1 02 c0 85 69 05 0d 09 38 75 08 95 01 15 00 25 02 b1 02 c0 06 00 ff 09 81 a1 01 85 17 75 08 95 1f 09 05 81 02 c0",
            input_info=(BusType.I2C, 0x04F3, 0x2A49),
        )


class TestGoodix_27c6_0e00(BaseTest.TestTablet):
    def create_device(self):
        return USIPen(
            "uhid test Elan_04f3_2A49",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 55 0e 65 11 35 00 15 00 09 42 25 01 75 01 95 01 81 02 25 7f 09 30 75 07 81 42 95 01 75 08 09 51 81 02 75 10 05 01 26 04 20 46 e6 09 09 30 81 02 26 60 15 46 9a 06 09 31 81 02 05 0d 55 0f 75 08 25 ff 45 ff 09 48 81 42 09 49 81 42 55 0e c0 09 22 a1 02 09 42 25 01 75 01 95 01 81 02 25 7f 09 30 75 07 81 42 95 01 75 08 09 51 81 02 75 10 05 01 26 04 20 46 e6 09 09 30 81 02 26 60 15 46 9a 06 09 31 81 02 05 0d 55 0f 75 08 25 ff 45 ff 09 48 81 42 09 49 81 42 55 0e c0 09 22 a1 02 09 42 25 01 75 01 95 01 81 02 25 7f 09 30 75 07 81 42 95 01 75 08 09 51 81 02 75 10 05 01 26 04 20 46 e6 09 09 30 81 02 26 60 15 46 9a 06 09 31 81 02 05 0d 55 0f 75 08 25 ff 45 ff 09 48 81 42 09 49 81 42 55 0e c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 30 75 07 81 42 75 08 09 51 95 01 81 02 05 01 26 04 20 75 10 55 0e 65 11 09 30 35 00 46 e6 09 81 02 26 60 15 46 9a 06 09 31 81 02 05 0d 55 0f 75 08 25 ff 45 ff 09 48 81 42 09 49 81 42 55 0e c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 30 75 07 81 42 75 08 09 51 95 01 81 02 05 01 26 04 20 75 10 55 0e 65 11 09 30 35 00 46 e6 09 81 02 26 60 15 46 9a 06 09 31 81 02 05 0d 55 0f 75 08 25 ff 45 ff 09 48 81 42 09 49 81 42 55 0e c0 09 54 15 00 25 7f 75 08 95 01 81 02 85 02 09 55 95 01 25 0a b1 02 85 03 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 02 a1 01 09 20 a1 00 85 08 05 01 a4 09 30 35 00 46 e6 09 15 00 26 04 20 55 0d 65 13 75 10 95 01 81 02 09 31 46 9a 06 26 60 15 81 02 b4 05 0d 09 38 95 01 75 08 15 00 25 01 81 02 09 30 75 10 26 ff 0f 81 02 09 31 81 02 09 42 09 44 09 5a 09 3c 09 45 09 32 75 01 95 06 25 01 81 02 95 02 81 03 09 3d 55 0e 65 14 36 d8 dc 46 28 23 16 d8 dc 26 28 23 95 01 75 10 81 02 09 3e 81 02 09 41 15 00 27 a0 8c 00 00 35 00 47 a0 8c 00 00 81 02 05 20 0a 53 04 65 00 16 01 f8 26 ff 07 75 10 95 01 81 02 0a 54 04 81 02 0a 55 04 81 02 0a 57 04 81 02 0a 58 04 81 02 0a 59 04 81 02 0a 72 04 81 02 0a 73 04 81 02 0a 74 04 81 02 05 0d 09 3b 15 00 25 64 75 08 81 02 09 5b 25 ff 75 40 81 02 06 00 ff 09 5b 75 20 81 02 05 0d 09 5c 26 ff 00 75 08 81 02 09 5e 81 02 09 70 a1 02 15 01 25 06 09 72 09 73 09 74 09 75 09 76 09 77 81 20 c0 06 00 ff 09 01 15 00 27 ff ff 00 00 75 10 95 01 81 02 85 09 09 81 a1 02 09 81 15 01 25 04 09 82 09 83 09 84 09 85 81 20 c0 85 10 09 5c a1 02 15 00 25 01 75 08 95 01 09 38 b1 02 09 5c 26 ff 00 b1 02 09 5d 75 01 95 01 25 01 b1 02 95 07 b1 03 c0 85 11 09 5e a1 02 09 38 15 00 25 01 75 08 95 01 b1 02 09 5e 26 ff 00 b1 02 09 5f 75 01 25 01 b1 02 75 07 b1 03 c0 85 12 09 70 a1 02 75 08 95 01 15 00 25 01 09 38 b1 02 09 70 a1 02 25 06 09 72 09 73 09 74 09 75 09 76 09 77 b1 20 c0 09 71 75 01 25 01 b1 02 75 07 b1 03 c0 85 13 09 80 15 00 25 ff 75 40 95 01 b1 02 85 14 09 44 a1 02 09 38 75 08 95 01 25 01 b1 02 15 01 25 03 09 44 a1 02 09 a4 09 44 09 5a 09 45 09 a3 b1 20 c0 09 5a a1 02 09 a4 09 44 09 5a 09 45 09 a3 b1 20 c0 09 45 a1 02 09 a4 09 44 09 5a 09 45 09 a3 b1 20 c0 c0 85 15 75 08 95 01 05 0d 09 90 a1 02 09 38 25 01 b1 02 09 91 75 10 26 ff 0f b1 02 09 92 75 40 25 ff b1 02 05 06 09 2a 75 08 26 ff 00 a1 02 09 2d b1 02 09 2e b1 02 c0 c0 85 16 05 06 09 2b a1 02 05 0d 25 01 09 38 b1 02 05 06 09 2b a1 02 09 2d 26 ff 00 b1 02 09 2e b1 02 c0 c0 85 17 06 00 ff 09 01 a1 02 05 0d 09 38 75 08 95 01 25 01 b1 02 06 00 ff 09 01 75 10 27 ff ff 00 00 b1 02 c0 85 18 05 0d 09 38 75 08 95 01 15 00 25 01 b1 02 c0 c0 06 f0 ff 09 01 a1 01 85 0e 09 01 15 00 25 ff 75 08 95 40 91 02 09 01 15 00 25 ff 75 08 95 40 81 02 c0",
            input_info=(BusType.I2C, 0x27C6, 0x0E00),
        )


class TestXPPen_ArtistPro16Gen2_28bd_095b(BaseTest.TestTablet):
    hid_bpfs = [HidBpf("XPPen__ArtistPro16Gen2.bpf.o", True)]

    def create_device(self):
        dev = XPPen_ArtistPro16Gen2_28bd_095b(
            "uhid test XPPen Artist Pro 16 Gen2 28bd 095b",
            rdesc="05 0d 09 02 a1 01 85 07 09 20 a1 00 09 42 09 44 09 45 09 3c 15 00 25 01 75 01 95 04 81 02 95 01 81 03 09 32 15 00 25 01 95 01 81 02 95 02 81 03 75 10 95 01 35 00 a4 05 01 09 30 65 13 55 0d 46 ff 34 26 ff 7f 81 02 09 31 46 20 21 26 ff 7f 81 02 b4 09 30 45 00 26 ff 3f 81 42 09 3d 15 81 25 7f 75 08 95 01 81 02 09 3e 15 81 25 7f 81 02 c0 c0",
            input_info=(BusType.USB, 0x28BD, 0x095B),
        )
        return dev


class TestXPPen_Artist24_28bd_093a(BaseTest.TestTablet):
    hid_bpfs = [HidBpf("XPPen__Artist24.bpf.o", True)]

    def create_device(self):
        return XPPen_Artist24_28bd_093a(
            "uhid test XPPen Artist 24 28bd 093a",
            rdesc="05 0d 09 02 a1 01 85 07 09 20 a1 00 09 42 09 44 09 45 15 00 25 01 75 01 95 03 81 02 95 02 81 03 09 32 95 01 81 02 95 02 81 03 75 10 95 01 35 00 a4 05 01 09 30 65 13 55 0d 46 f0 50 26 ff 7f 81 02 09 31 46 91 2d 26 ff 7f 81 02 b4 09 30 45 00 26 ff 1f 81 42 09 3d 15 81 25 7f 75 08 95 01 81 02 09 3e 15 81 25 7f 81 02 c0 c0",
            input_info=(BusType.USB, 0x28BD, 0x093A),
        )


class TestHuion_Kamvas_Pro_19_256c_006b(BaseTest.TestTablet):
    hid_bpfs = [HidBpf("Huion__Kamvas-Pro-19.bpf.o", True)]

    def create_device(self):
        return Huion_Kamvas_Pro_19_256c_006b(
            "uhid test HUION Huion Tablet_GT1902",
            rdesc="05 0d 09 02 a1 01 85 0a 09 20 a1 01 09 42 09 44 09 43 09 3c 09 45 15 00 25 01 75 01 95 06 81 02 09 32 75 01 95 01 81 02 81 03 05 01 09 30 09 31 55 0d 65 33 26 ff 7f 35 00 46 00 08 75 10 95 02 81 02 05 0d 09 30 26 ff 3f 75 10 95 01 81 02 09 3d 09 3e 15 a6 25 5a 75 08 95 02 81 02 c0 c0 05 0d 09 04 a1 01 85 04 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 ff 7f 35 00 46 15 0c 81 42 09 31 26 ff 7f 46 cb 06 81 42 05 0d 09 30 26 ff 1f 75 10 95 01 81 02 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 ff 7f 35 00 46 15 0c 81 42 09 31 26 ff 7f 46 cb 06 81 42 05 0d 09 30 26 ff 1f 75 10 95 01 81 02 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 75 08 95 08 81 03 85 05 09 55 25 0a 75 08 95 01 b1 02 06 00 ff 09 c5 85 06 15 00 26 ff 00 75 08 96 00 01 b1 02 c0",
            input_info=(BusType.USB, 0x256C, 0x006B),
        )
