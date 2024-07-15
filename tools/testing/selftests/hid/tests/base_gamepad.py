# SPDX-License-Identifier: GPL-2.0
import libevdev

from .base_device import BaseDevice
from hidtools.util import BusType


class InvalidHIDCommunication(Exception):
    pass


class GamepadData(object):
    pass


class AxisMapping(object):
    """Represents a mapping between a HID type
    and an evdev event"""

    def __init__(self, hid, evdev=None):
        self.hid = hid.lower()

        if evdev is None:
            evdev = f"ABS_{hid.upper()}"

        self.evdev = libevdev.evbit("EV_ABS", evdev)


class BaseGamepad(BaseDevice):
    buttons_map = {
        1: "BTN_SOUTH",
        2: "BTN_EAST",
        3: "BTN_C",
        4: "BTN_NORTH",
        5: "BTN_WEST",
        6: "BTN_Z",
        7: "BTN_TL",
        8: "BTN_TR",
        9: "BTN_TL2",
        10: "BTN_TR2",
        11: "BTN_SELECT",
        12: "BTN_START",
        13: "BTN_MODE",
        14: "BTN_THUMBL",
        15: "BTN_THUMBR",
    }

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

    def __init__(self, rdesc, application="Game Pad", name=None, input_info=None):
        assert rdesc is not None
        super().__init__(name, application, input_info=input_info, rdesc=rdesc)
        self.buttons = (1, 2, 3)
        self._buttons = {}
        self.left = (127, 127)
        self.right = (127, 127)
        self.hat_switch = 15
        assert self.parsed_rdesc is not None

        self.fields = []
        for r in self.parsed_rdesc.input_reports.values():
            if r.application_name == self.application:
                self.fields.extend([f.usage_name for f in r])

    def store_axes(self, which, gamepad, data):
        amap = self.axes_map[which]
        x, y = data
        setattr(gamepad, amap["x"].hid, x)
        setattr(gamepad, amap["y"].hid, y)

    def create_report(
        self,
        *,
        left=(None, None),
        right=(None, None),
        hat_switch=None,
        buttons=None,
        reportID=None,
        application="Game Pad",
    ):
        """
        Return an input report for this device.

        :param left: a tuple of absolute (x, y) value of the left joypad
            where ``None`` is "leave unchanged"
        :param right: a tuple of absolute (x, y) value of the right joypad
            where ``None`` is "leave unchanged"
        :param hat_switch: an absolute angular value of the hat switch
            (expressed in 1/8 of circle, 0 being North, 2 East)
            where ``None`` is "leave unchanged"
        :param buttons: a dict of index/bool for the button states,
            where ``None`` is "leave unchanged"
        :param reportID: the numeric report ID for this report, if needed
        :param application: the application used to report the values
        """
        if buttons is not None:
            for i, b in buttons.items():
                if i not in self.buttons:
                    raise InvalidHIDCommunication(
                        f"button {i} is not part of this {self.application}"
                    )
                if b is not None:
                    self._buttons[i] = b

        def replace_none_in_tuple(item, default):
            if item is None:
                item = (None, None)

            if None in item:
                if item[0] is None:
                    item = (default[0], item[1])
                if item[1] is None:
                    item = (item[0], default[1])

            return item

        right = replace_none_in_tuple(right, self.right)
        self.right = right
        left = replace_none_in_tuple(left, self.left)
        self.left = left

        if hat_switch is None:
            hat_switch = self.hat_switch
        else:
            self.hat_switch = hat_switch

        reportID = reportID or self.default_reportID

        gamepad = GamepadData()
        for i, b in self._buttons.items():
            gamepad.__setattr__(f"b{i}", int(b) if b is not None else 0)

        self.store_axes("left_stick", gamepad, left)
        self.store_axes("right_stick", gamepad, right)
        gamepad.hatswitch = hat_switch  # type: ignore  ### gamepad is by default empty
        return super().create_report(
            gamepad, reportID=reportID, application=application
        )

    def event(
        self, *, left=(None, None), right=(None, None), hat_switch=None, buttons=None
    ):
        """
        Send an input event on the default report ID.

        :param left: a tuple of absolute (x, y) value of the left joypad
            where ``None`` is "leave unchanged"
        :param right: a tuple of absolute (x, y) value of the right joypad
            where ``None`` is "leave unchanged"
        :param hat_switch: an absolute angular value of the hat switch
            where ``None`` is "leave unchanged"
        :param buttons: a dict of index/bool for the button states,
            where ``None`` is "leave unchanged"
        """
        r = self.create_report(
            left=left, right=right, hat_switch=hat_switch, buttons=buttons
        )
        self.call_input_event(r)
        return [r]


class JoystickGamepad(BaseGamepad):
    buttons_map = {
        1: "BTN_TRIGGER",
        2: "BTN_THUMB",
        3: "BTN_THUMB2",
        4: "BTN_TOP",
        5: "BTN_TOP2",
        6: "BTN_PINKIE",
        7: "BTN_BASE",
        8: "BTN_BASE2",
        9: "BTN_BASE3",
        10: "BTN_BASE4",
        11: "BTN_BASE5",
        12: "BTN_BASE6",
        13: "BTN_DEAD",
    }

    axes_map = {
        "left_stick": {
            "x": AxisMapping("x"),
            "y": AxisMapping("y"),
        },
        "right_stick": {
            "x": AxisMapping("rudder"),
            "y": AxisMapping("throttle"),
        },
    }

    def __init__(self, rdesc, application="Joystick", name=None, input_info=None):
        super().__init__(rdesc, application, name, input_info)

    def create_report(
        self,
        *,
        left=(None, None),
        right=(None, None),
        hat_switch=None,
        buttons=None,
        reportID=None,
        application=None,
    ):
        """
        Return an input report for this device.

        :param left: a tuple of absolute (x, y) value of the left joypad
            where ``None`` is "leave unchanged"
        :param right: a tuple of absolute (x, y) value of the right joypad
            where ``None`` is "leave unchanged"
        :param hat_switch: an absolute angular value of the hat switch
            where ``None`` is "leave unchanged"
        :param buttons: a dict of index/bool for the button states,
            where ``None`` is "leave unchanged"
        :param reportID: the numeric report ID for this report, if needed
        :param application: the application for this report, if needed
        """
        if application is None:
            application = "Joystick"
        return super().create_report(
            left=left,
            right=right,
            hat_switch=hat_switch,
            buttons=buttons,
            reportID=reportID,
            application=application,
        )

    def store_right_joystick(self, gamepad, data):
        gamepad.rudder, gamepad.throttle = data
