#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2017 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2017 Red Hat, Inc.
# Copyright (c) 2020 Wacom Technology Corp.
#
# Authors:
#     Jason Gerecke <jason.gerecke@wacom.com>

"""
Tests for the Wacom driver generic codepath.

This module tests the function of the Wacom driver's generic codepath.
The generic codepath is used by devices which are not explicitly listed
in the driver's device table. It uses the device's HID descriptor to
decode reports sent by the device.
"""

from .descriptors_wacom import (
    wacom_pth660_v145,
    wacom_pth660_v150,
    wacom_pth860_v145,
    wacom_pth860_v150,
    wacom_pth460_v105,
)

import attr
from enum import Enum
from hidtools.hut import HUT
from hidtools.hid import HidUnit
from . import base
from . import test_multitouch
import libevdev
import pytest

import logging

logger = logging.getLogger("hidtools.test.wacom")

KERNEL_MODULE = ("wacom", "wacom")


class ProximityState(Enum):
    """
    Enumeration of allowed proximity states.
    """

    # Tool is not able to be sensed by the device
    OUT = 0

    # Tool is close enough to be sensed, but some data may be invalid
    # or inaccurate
    IN_PROXIMITY = 1

    # Tool is close enough to be sensed with high accuracy. All data
    # valid.
    IN_RANGE = 2

    def fill(self, reportdata):
        """Fill a report with approrpiate HID properties/values."""
        reportdata.inrange = self in [ProximityState.IN_RANGE]
        reportdata.wacomsense = self in [
            ProximityState.IN_PROXIMITY,
            ProximityState.IN_RANGE,
        ]


class ReportData:
    """
    Placeholder for HID report values.
    """

    pass


@attr.s
class Buttons:
    """
    Stylus button state.

    Describes the state of each of the buttons / "side switches" that
    may be present on a stylus. Buttons set to 'None' indicate the
    state is "unchanged" since the previous event.
    """

    primary = attr.ib(default=None)
    secondary = attr.ib(default=None)
    tertiary = attr.ib(default=None)

    @staticmethod
    def clear():
        """Button object with all states cleared."""
        return Buttons(False, False, False)

    def fill(self, reportdata):
        """Fill a report with approrpiate HID properties/values."""
        reportdata.barrelswitch = int(self.primary or 0)
        reportdata.secondarybarrelswitch = int(self.secondary or 0)
        reportdata.b3 = int(self.tertiary or 0)


@attr.s
class ToolID:
    """
    Stylus tool identifiers.

    Contains values used to identify a specific stylus, e.g. its serial
    number and tool-type identifier. Values of ``0`` may sometimes be
    used for the out-of-range condition.
    """

    serial = attr.ib()
    tooltype = attr.ib()

    @staticmethod
    def clear():
        """ToolID object with all fields cleared."""
        return ToolID(0, 0)

    def fill(self, reportdata):
        """Fill a report with approrpiate HID properties/values."""
        reportdata.transducerserialnumber = self.serial & 0xFFFFFFFF
        reportdata.serialhi = (self.serial >> 32) & 0xFFFFFFFF
        reportdata.tooltype = self.tooltype


@attr.s
class PhysRange:
    """
    Range of HID physical values, with units.
    """

    unit = attr.ib()
    min_size = attr.ib()
    max_size = attr.ib()

    CENTIMETER = HidUnit.from_string("SILinear: cm")
    DEGREE = HidUnit.from_string("EnglishRotation: deg")

    def contains(self, field):
        """
        Check if the physical size of the provided field is in range.

        Compare the physical size described by the provided HID field
        against the range of sizes described by this object. This is
        an exclusive range comparison (e.g. 0 cm is not within the
        range 0 cm - 5 cm) and exact unit comparison (e.g. 1 inch is
        not within the range 0 cm - 5 cm).
        """
        phys_size = (field.physical_max - field.physical_min) * 10 ** (field.unit_exp)
        return (
            field.unit == self.unit.value
            and phys_size > self.min_size
            and phys_size < self.max_size
        )


class BaseTablet(base.UHIDTestDevice):
    """
    Skeleton object for all kinds of tablet devices.
    """

    def __init__(self, rdesc, name=None, info=None):
        assert rdesc is not None
        super().__init__(name, "Pen", input_info=info, rdesc=rdesc)
        self.buttons = Buttons.clear()
        self.toolid = ToolID.clear()
        self.proximity = ProximityState.OUT
        self.offset = 0
        self.ring = -1
        self.ek0 = False

    def match_evdev_rule(self, application, evdev):
        """
        Filter out evdev nodes based on the requested application.

        The Wacom driver may create several device nodes for each USB
        interface device. It is crucial that we run tests with the
        expected device node or things will obviously go off the rails.
        Use the Wacom driver's usual naming conventions to apply a
        sensible default filter.
        """
        if application in ["Pen", "Pad"]:
            return evdev.name.endswith(application)
        else:
            return True

    def create_report(
        self, x, y, pressure, buttons=None, toolid=None, proximity=None, reportID=None
    ):
        """
        Return an input report for this device.

        :param x: absolute x
        :param y: absolute y
        :param pressure: pressure
        :param buttons: stylus button state. Use ``None`` for unchanged.
        :param toolid: tool identifiers. Use ``None`` for unchanged.
        :param proximity: a ProximityState indicating the sensor's ability
             to detect and report attributes of this tool. Use ``None``
             for unchanged.
        :param reportID: the numeric report ID for this report, if needed
        """
        if buttons is not None:
            self.buttons = buttons
        buttons = self.buttons

        if toolid is not None:
            self.toolid = toolid
        toolid = self.toolid

        if proximity is not None:
            self.proximity = proximity
        proximity = self.proximity

        reportID = reportID or self.default_reportID

        report = ReportData()
        report.x = x
        report.y = y
        report.tippressure = pressure
        report.tipswitch = pressure > 0
        buttons.fill(report)
        proximity.fill(report)
        toolid.fill(report)

        return super().create_report(report, reportID=reportID)

    def create_report_heartbeat(self, reportID):
        """
        Return a heartbeat input report for this device.

        Heartbeat reports generally contain battery status information,
        among other things.
        """
        report = ReportData()
        report.wacombatterycharging = 1
        return super().create_report(report, reportID=reportID)

    def create_report_pad(self, reportID, ring, ek0):
        report = ReportData()

        if ring is not None:
            self.ring = ring
        ring = self.ring

        if ek0 is not None:
            self.ek0 = ek0
        ek0 = self.ek0

        if ring >= 0:
            report.wacomtouchring = ring
            report.wacomtouchringstatus = 1
        else:
            report.wacomtouchring = 0x7F
            report.wacomtouchringstatus = 0

        report.wacomexpresskey00 = ek0
        return super().create_report(report, reportID=reportID)

    def event(self, x, y, pressure, buttons=None, toolid=None, proximity=None):
        """
        Send an input event on the default report ID.

        :param x: absolute x
        :param y: absolute y
        :param buttons: stylus button state. Use ``None`` for unchanged.
        :param toolid: tool identifiers. Use ``None`` for unchanged.
        :param proximity: a ProximityState indicating the sensor's ability
             to detect and report attributes of this tool. Use ``None``
             for unchanged.
        """
        r = self.create_report(x, y, pressure, buttons, toolid, proximity)
        self.call_input_event(r)
        return [r]

    def event_heartbeat(self, reportID):
        """
        Send a heartbeat event on the requested report ID.
        """
        r = self.create_report_heartbeat(reportID)
        self.call_input_event(r)
        return [r]

    def event_pad(self, reportID, ring=None, ek0=None):
        """
        Send a pad event on the requested report ID.
        """
        r = self.create_report_pad(reportID, ring, ek0)
        self.call_input_event(r)
        return [r]

    def get_report(self, req, rnum, rtype):
        if rtype != self.UHID_FEATURE_REPORT:
            return (1, [])

        rdesc = None
        for v in self.parsed_rdesc.feature_reports.values():
            if v.report_ID == rnum:
                rdesc = v

        if rdesc is None:
            return (1, [])

        result = (1, [])
        result = self.create_report_offset(rdesc) or result
        return result

    def create_report_offset(self, rdesc):
        require = [
            "Wacom Offset Left",
            "Wacom Offset Top",
            "Wacom Offset Right",
            "Wacom Offset Bottom",
        ]
        if not set(require).issubset(set([f.usage_name for f in rdesc])):
            return None

        report = ReportData()
        report.wacomoffsetleft = self.offset
        report.wacomoffsettop = self.offset
        report.wacomoffsetright = self.offset
        report.wacomoffsetbottom = self.offset
        r = rdesc.create_report([report], None)
        return (0, r)


class OpaqueTablet(BaseTablet):
    """
    Bare-bones opaque tablet with a minimum of features.

    A tablet stripped down to its absolute core. It is capable of
    reporting X/Y position and if the pen is in contact. No pressure,
    no barrel switches, no eraser. Notably it *does* report an "In
    Range" flag, but this is only because the Wacom driver expects
    one to function properly. The device uses only standard HID usages,
    not any of Wacom's vendor-defined pages.
    """

    # fmt: off
    report_descriptor = [
        0x05, 0x0D,                     # . Usage Page (Digitizer),
        0x09, 0x01,                     # . Usage (Digitizer),
        0xA1, 0x01,                     # . Collection (Application),
        0x85, 0x01,                     # .     Report ID (1),
        0x09, 0x20,                     # .     Usage (Stylus),
        0xA1, 0x00,                     # .     Collection (Physical),
        0x09, 0x42,                     # .         Usage (Tip Switch),
        0x09, 0x32,                     # .         Usage (In Range),
        0x15, 0x00,                     # .         Logical Minimum (0),
        0x25, 0x01,                     # .         Logical Maximum (1),
        0x75, 0x01,                     # .         Report Size (1),
        0x95, 0x02,                     # .         Report Count (2),
        0x81, 0x02,                     # .         Input (Variable),
        0x95, 0x06,                     # .         Report Count (6),
        0x81, 0x03,                     # .         Input (Constant, Variable),
        0x05, 0x01,                     # .         Usage Page (Desktop),
        0x09, 0x30,                     # .         Usage (X),
        0x27, 0x80, 0x3E, 0x00, 0x00,   # .         Logical Maximum (16000),
        0x47, 0x80, 0x3E, 0x00, 0x00,   # .         Physical Maximum (16000),
        0x65, 0x11,                     # .         Unit (Centimeter),
        0x55, 0x0D,                     # .         Unit Exponent (13),
        0x75, 0x10,                     # .         Report Size (16),
        0x95, 0x01,                     # .         Report Count (1),
        0x81, 0x02,                     # .         Input (Variable),
        0x09, 0x31,                     # .         Usage (Y),
        0x27, 0x28, 0x23, 0x00, 0x00,   # .         Logical Maximum (9000),
        0x47, 0x28, 0x23, 0x00, 0x00,   # .         Physical Maximum (9000),
        0x81, 0x02,                     # .         Input (Variable),
        0xC0,                           # .     End Collection,
        0xC0,                           # . End Collection,
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, info=(0x3, 0x056A, 0x9999)):
        super().__init__(rdesc, name, info)
        self.default_reportID = 1


class OpaqueCTLTablet(BaseTablet):
    """
    Opaque tablet similar to something in the CTL product line.

    A pen-only tablet with most basic features you would expect from
    an actual device. Position, eraser, pressure, barrel buttons.
    Uses the Wacom vendor-defined usage page.
    """

    # fmt: off
    report_descriptor = [
        0x06, 0x0D, 0xFF,               # . Usage Page (Vnd Wacom Emr),
        0x09, 0x01,                     # . Usage (Digitizer),
        0xA1, 0x01,                     # . Collection (Application),
        0x85, 0x10,                     # .     Report ID (16),
        0x09, 0x20,                     # .     Usage (Stylus),
        0x35, 0x00,                     # .     Physical Minimum (0),
        0x45, 0x00,                     # .     Physical Maximum (0),
        0x15, 0x00,                     # .     Logical Minimum (0),
        0x25, 0x01,                     # .     Logical Maximum (1),
        0xA1, 0x00,                     # .     Collection (Physical),
        0x09, 0x42,                     # .         Usage (Tip Switch),
        0x09, 0x44,                     # .         Usage (Barrel Switch),
        0x09, 0x5A,                     # .         Usage (Secondary Barrel Switch),
        0x09, 0x45,                     # .         Usage (Eraser),
        0x09, 0x3C,                     # .         Usage (Invert),
        0x09, 0x32,                     # .         Usage (In Range),
        0x09, 0x36,                     # .         Usage (In Proximity),
        0x25, 0x01,                     # .         Logical Maximum (1),
        0x75, 0x01,                     # .         Report Size (1),
        0x95, 0x07,                     # .         Report Count (7),
        0x81, 0x02,                     # .         Input (Variable),
        0x95, 0x01,                     # .         Report Count (1),
        0x81, 0x03,                     # .         Input (Constant, Variable),
        0x0A, 0x30, 0x01,               # .         Usage (X),
        0x65, 0x11,                     # .         Unit (Centimeter),
        0x55, 0x0D,                     # .         Unit Exponent (13),
        0x47, 0x80, 0x3E, 0x00, 0x00,   # .         Physical Maximum (16000),
        0x27, 0x80, 0x3E, 0x00, 0x00,   # .         Logical Maximum (16000),
        0x75, 0x18,                     # .         Report Size (24),
        0x95, 0x01,                     # .         Report Count (1),
        0x81, 0x02,                     # .         Input (Variable),
        0x0A, 0x31, 0x01,               # .         Usage (Y),
        0x47, 0x28, 0x23, 0x00, 0x00,   # .         Physical Maximum (9000),
        0x27, 0x28, 0x23, 0x00, 0x00,   # .         Logical Maximum (9000),
        0x81, 0x02,                     # .         Input (Variable),
        0x09, 0x30,                     # .         Usage (Tip Pressure),
        0x55, 0x00,                     # .         Unit Exponent (0),
        0x65, 0x00,                     # .         Unit,
        0x47, 0x00, 0x00, 0x00, 0x00,   # .         Physical Maximum (0),
        0x26, 0xFF, 0x0F,               # .         Logical Maximum (4095),
        0x75, 0x10,                     # .         Report Size (16),
        0x81, 0x02,                     # .         Input (Variable),
        0x75, 0x08,                     # .         Report Size (8),
        0x95, 0x06,                     # .         Report Count (6),
        0x81, 0x03,                     # .         Input (Constant, Variable),
        0x0A, 0x32, 0x01,               # .         Usage (Z),
        0x25, 0x3F,                     # .         Logical Maximum (63),
        0x75, 0x08,                     # .         Report Size (8),
        0x95, 0x01,                     # .         Report Count (1),
        0x81, 0x02,                     # .         Input (Variable),
        0x09, 0x5B,                     # .         Usage (Transducer Serial Number),
        0x09, 0x5C,                     # .         Usage (Transducer Serial Number Hi),
        0x17, 0x00, 0x00, 0x00, 0x80,   # .         Logical Minimum (-2147483648),
        0x27, 0xFF, 0xFF, 0xFF, 0x7F,   # .         Logical Maximum (2147483647),
        0x75, 0x20,                     # .         Report Size (32),
        0x95, 0x02,                     # .         Report Count (2),
        0x81, 0x02,                     # .         Input (Variable),
        0x09, 0x77,                     # .         Usage (Tool Type),
        0x15, 0x00,                     # .         Logical Minimum (0),
        0x26, 0xFF, 0x0F,               # .         Logical Maximum (4095),
        0x75, 0x10,                     # .         Report Size (16),
        0x95, 0x01,                     # .         Report Count (1),
        0x81, 0x02,                     # .         Input (Variable),
        0xC0,                           # .     End Collection,
        0xC0                            # . End Collection
    ]
    # fmt: on

    def __init__(self, rdesc=report_descriptor, name=None, info=(0x3, 0x056A, 0x9999)):
        super().__init__(rdesc, name, info)
        self.default_reportID = 16


class PTHX60_Pen(BaseTablet):
    """
    Pen interface of a PTH-660 / PTH-860 / PTH-460 tablet.

    This generation of devices are nearly identical to each other, though
    the PTH-460 uses a slightly different descriptor construction (splits
    the pad among several physical collections)
    """

    def __init__(self, rdesc=None, name=None, info=None):
        super().__init__(rdesc, name, info)
        self.default_reportID = 16


class BaseTest:
    class TestTablet(base.BaseTestCase.TestUhid):
        kernel_modules = [KERNEL_MODULE]

        def sync_and_assert_events(
            self, report, expected_events, auto_syn=True, strict=False
        ):
            """
            Assert we see the expected events in response to a report.
            """
            uhdev = self.uhdev
            syn_event = self.syn_event
            if auto_syn:
                expected_events.append(syn_event)
            actual_events = uhdev.next_sync_events()
            self.debug_reports(report, uhdev, actual_events)
            if strict:
                self.assertInputEvents(expected_events, actual_events)
            else:
                self.assertInputEventsIn(expected_events, actual_events)

        def get_usages(self, uhdev):
            def get_report_usages(report):
                application = report.application
                for field in report.fields:
                    if field.usages is not None:
                        for usage in field.usages:
                            yield (field, usage, application)
                    else:
                        yield (field, field.usage, application)

            desc = uhdev.parsed_rdesc
            reports = [
                *desc.input_reports.values(),
                *desc.feature_reports.values(),
                *desc.output_reports.values(),
            ]
            for report in reports:
                for usage in get_report_usages(report):
                    yield usage

        def assertName(self, uhdev, type):
            """
            Assert that the name is as we expect.

            The Wacom driver applies a number of decorations to the name
            provided by the hardware. We cannot rely on the definition of
            this assertion from the base class to work properly.
            """
            evdev = uhdev.get_evdev()
            expected_name = uhdev.name + type
            if "wacom" not in expected_name.lower():
                expected_name = "Wacom " + expected_name
            assert evdev.name == expected_name

        def test_descriptor_physicals(self):
            """
            Verify that all HID usages which should have a physical range
            actually do, and those which shouldn't don't. Also verify that
            the associated unit is correct and within a sensible range.
            """

            def usage_id(page_name, usage_name):
                page = HUT.usage_page_from_name(page_name)
                return (page.page_id << 16) | page[usage_name].usage

            required = {
                usage_id("Generic Desktop", "X"): PhysRange(
                    PhysRange.CENTIMETER, 5, 150
                ),
                usage_id("Generic Desktop", "Y"): PhysRange(
                    PhysRange.CENTIMETER, 5, 150
                ),
                usage_id("Digitizers", "Width"): PhysRange(
                    PhysRange.CENTIMETER, 5, 150
                ),
                usage_id("Digitizers", "Height"): PhysRange(
                    PhysRange.CENTIMETER, 5, 150
                ),
                usage_id("Digitizers", "X Tilt"): PhysRange(PhysRange.DEGREE, 90, 180),
                usage_id("Digitizers", "Y Tilt"): PhysRange(PhysRange.DEGREE, 90, 180),
                usage_id("Digitizers", "Twist"): PhysRange(PhysRange.DEGREE, 358, 360),
                usage_id("Wacom", "X Tilt"): PhysRange(PhysRange.DEGREE, 90, 180),
                usage_id("Wacom", "Y Tilt"): PhysRange(PhysRange.DEGREE, 90, 180),
                usage_id("Wacom", "Twist"): PhysRange(PhysRange.DEGREE, 358, 360),
                usage_id("Wacom", "X"): PhysRange(PhysRange.CENTIMETER, 5, 150),
                usage_id("Wacom", "Y"): PhysRange(PhysRange.CENTIMETER, 5, 150),
                usage_id("Wacom", "Wacom TouchRing"): PhysRange(
                    PhysRange.DEGREE, 358, 360
                ),
                usage_id("Wacom", "Wacom Offset Left"): PhysRange(
                    PhysRange.CENTIMETER, 0, 0.5
                ),
                usage_id("Wacom", "Wacom Offset Top"): PhysRange(
                    PhysRange.CENTIMETER, 0, 0.5
                ),
                usage_id("Wacom", "Wacom Offset Right"): PhysRange(
                    PhysRange.CENTIMETER, 0, 0.5
                ),
                usage_id("Wacom", "Wacom Offset Bottom"): PhysRange(
                    PhysRange.CENTIMETER, 0, 0.5
                ),
            }
            for field, usage, application in self.get_usages(self.uhdev):
                if application == usage_id("Generic Desktop", "Mouse"):
                    # Ignore the vestigial Mouse collection which exists
                    # on Wacom tablets only for backwards compatibility.
                    continue

                expect_physical = usage in required

                phys_set = field.physical_min != 0 or field.physical_max != 0
                assert phys_set == expect_physical

                unit_set = field.unit != 0
                assert unit_set == expect_physical

                if unit_set:
                    assert required[usage].contains(field)

        def test_prop_direct(self):
            """
            Todo: Verify that INPUT_PROP_DIRECT is set on display devices.
            """
            pass

        def test_prop_pointer(self):
            """
            Todo: Verify that INPUT_PROP_POINTER is set on opaque devices.
            """
            pass


class PenTabletTest(BaseTest.TestTablet):
    def assertName(self, uhdev):
        super().assertName(uhdev, " Pen")


class TouchTabletTest(BaseTest.TestTablet):
    def assertName(self, uhdev):
        super().assertName(uhdev, " Finger")


class TestOpaqueTablet(PenTabletTest):
    def create_device(self):
        return OpaqueTablet()

    def test_sanity(self):
        """
        Bring a pen into contact with the tablet, then remove it.

        Ensure that we get the basic tool/touch/motion events that should
        be sent by the driver.
        """
        uhdev = self.uhdev

        self.sync_and_assert_events(
            uhdev.event(
                100,
                200,
                pressure=300,
                buttons=Buttons.clear(),
                toolid=ToolID(serial=1, tooltype=1),
                proximity=ProximityState.IN_RANGE,
            ),
            [
                libevdev.InputEvent(libevdev.EV_KEY.BTN_TOOL_PEN, 1),
                libevdev.InputEvent(libevdev.EV_ABS.ABS_X, 100),
                libevdev.InputEvent(libevdev.EV_ABS.ABS_Y, 200),
                libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 1),
            ],
        )

        self.sync_and_assert_events(
            uhdev.event(110, 220, pressure=0),
            [
                libevdev.InputEvent(libevdev.EV_ABS.ABS_X, 110),
                libevdev.InputEvent(libevdev.EV_ABS.ABS_Y, 220),
                libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 0),
            ],
        )

        self.sync_and_assert_events(
            uhdev.event(
                120,
                230,
                pressure=0,
                toolid=ToolID.clear(),
                proximity=ProximityState.OUT,
            ),
            [
                libevdev.InputEvent(libevdev.EV_KEY.BTN_TOOL_PEN, 0),
            ],
        )

        self.sync_and_assert_events(
            uhdev.event(130, 240, pressure=0), [], auto_syn=False, strict=True
        )


class TestOpaqueCTLTablet(TestOpaqueTablet):
    def create_device(self):
        return OpaqueCTLTablet()

    def test_buttons(self):
        """
        Test that the barrel buttons (side switches) work as expected.

        Press and release each button individually to verify that we get
        the expected events.
        """
        uhdev = self.uhdev

        self.sync_and_assert_events(
            uhdev.event(
                100,
                200,
                pressure=0,
                buttons=Buttons.clear(),
                toolid=ToolID(serial=1, tooltype=1),
                proximity=ProximityState.IN_RANGE,
            ),
            [
                libevdev.InputEvent(libevdev.EV_KEY.BTN_TOOL_PEN, 1),
                libevdev.InputEvent(libevdev.EV_ABS.ABS_X, 100),
                libevdev.InputEvent(libevdev.EV_ABS.ABS_Y, 200),
                libevdev.InputEvent(libevdev.EV_MSC.MSC_SERIAL, 1),
            ],
        )

        self.sync_and_assert_events(
            uhdev.event(100, 200, pressure=0, buttons=Buttons(primary=True)),
            [
                libevdev.InputEvent(libevdev.EV_KEY.BTN_STYLUS, 1),
                libevdev.InputEvent(libevdev.EV_MSC.MSC_SERIAL, 1),
            ],
        )

        self.sync_and_assert_events(
            uhdev.event(100, 200, pressure=0, buttons=Buttons(primary=False)),
            [
                libevdev.InputEvent(libevdev.EV_KEY.BTN_STYLUS, 0),
                libevdev.InputEvent(libevdev.EV_MSC.MSC_SERIAL, 1),
            ],
        )

        self.sync_and_assert_events(
            uhdev.event(100, 200, pressure=0, buttons=Buttons(secondary=True)),
            [
                libevdev.InputEvent(libevdev.EV_KEY.BTN_STYLUS2, 1),
                libevdev.InputEvent(libevdev.EV_MSC.MSC_SERIAL, 1),
            ],
        )

        self.sync_and_assert_events(
            uhdev.event(100, 200, pressure=0, buttons=Buttons(secondary=False)),
            [
                libevdev.InputEvent(libevdev.EV_KEY.BTN_STYLUS2, 0),
                libevdev.InputEvent(libevdev.EV_MSC.MSC_SERIAL, 1),
            ],
        )


PTHX60_Devices = [
    {"rdesc": wacom_pth660_v145, "info": (0x3, 0x056A, 0x0357)},
    {"rdesc": wacom_pth660_v150, "info": (0x3, 0x056A, 0x0357)},
    {"rdesc": wacom_pth860_v145, "info": (0x3, 0x056A, 0x0358)},
    {"rdesc": wacom_pth860_v150, "info": (0x3, 0x056A, 0x0358)},
    {"rdesc": wacom_pth460_v105, "info": (0x3, 0x056A, 0x0392)},
]

PTHX60_Names = [
    "PTH-660/v145",
    "PTH-660/v150",
    "PTH-860/v145",
    "PTH-860/v150",
    "PTH-460/v105",
]


class TestPTHX60_Pen(TestOpaqueCTLTablet):
    @pytest.fixture(
        autouse=True, scope="class", params=PTHX60_Devices, ids=PTHX60_Names
    )
    def set_device_params(self, request):
        request.cls.device_params = request.param

    def create_device(self):
        return PTHX60_Pen(**self.device_params)

    @pytest.mark.xfail
    def test_descriptor_physicals(self):
        # XFAIL: Various documented errata
        super().test_descriptor_physicals()

    def test_heartbeat_spurious(self):
        """
        Test that the heartbeat report does not send spurious events.
        """
        uhdev = self.uhdev

        self.sync_and_assert_events(
            uhdev.event(
                100,
                200,
                pressure=300,
                buttons=Buttons.clear(),
                toolid=ToolID(serial=1, tooltype=0x822),
                proximity=ProximityState.IN_RANGE,
            ),
            [
                libevdev.InputEvent(libevdev.EV_KEY.BTN_TOOL_PEN, 1),
                libevdev.InputEvent(libevdev.EV_ABS.ABS_X, 100),
                libevdev.InputEvent(libevdev.EV_ABS.ABS_Y, 200),
                libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 1),
            ],
        )

        # Exactly zero events: not even a SYN
        self.sync_and_assert_events(
            uhdev.event_heartbeat(19), [], auto_syn=False, strict=True
        )

        self.sync_and_assert_events(
            uhdev.event(110, 200, pressure=300),
            [
                libevdev.InputEvent(libevdev.EV_ABS.ABS_X, 110),
            ],
        )

    def test_empty_pad_sync(self):
        self.empty_pad_sync(num=3, denom=16, reverse=True)

    def empty_pad_sync(self, num, denom, reverse):
        """
        Test that multiple pad collections do not trigger empty syncs.
        """

        def offset_rotation(value):
            """
            Offset touchring rotation values by the same factor as the
            Linux kernel. Tablets historically don't use the same origin
            as HID, and it sometimes changes from tablet to tablet...
            """
            evdev = self.uhdev.get_evdev()
            info = evdev.absinfo[libevdev.EV_ABS.ABS_WHEEL]
            delta = info.maximum - info.minimum + 1
            if reverse:
                value = info.maximum - value
            value += num * delta // denom
            if value > info.maximum:
                value -= delta
            elif value < info.minimum:
                value += delta
            return value

        uhdev = self.uhdev
        uhdev.application = "Pad"
        evdev = uhdev.get_evdev()

        print(evdev.name)
        self.sync_and_assert_events(
            uhdev.event_pad(reportID=17, ring=0, ek0=1),
            [
                libevdev.InputEvent(libevdev.EV_KEY.BTN_0, 1),
                libevdev.InputEvent(libevdev.EV_ABS.ABS_WHEEL, offset_rotation(0)),
                libevdev.InputEvent(libevdev.EV_ABS.ABS_MISC, 15),
            ],
        )

        self.sync_and_assert_events(
            uhdev.event_pad(reportID=17, ring=1, ek0=1),
            [libevdev.InputEvent(libevdev.EV_ABS.ABS_WHEEL, offset_rotation(1))],
        )

        self.sync_and_assert_events(
            uhdev.event_pad(reportID=17, ring=2, ek0=0),
            [
                libevdev.InputEvent(libevdev.EV_ABS.ABS_WHEEL, offset_rotation(2)),
                libevdev.InputEvent(libevdev.EV_KEY.BTN_0, 0),
            ],
        )


class TestDTH2452Tablet(test_multitouch.BaseTest.TestMultitouch, TouchTabletTest):
    def create_device(self):
        return test_multitouch.Digitizer(
            "DTH 2452",
            rdesc="05 0d 09 04 a1 01 85 0c 95 01 75 08 15 00 26 ff 00 81 03 09 54 81 02 09 22 a1 02 05 0d 95 01 75 01 25 01 09 42 81 02 81 03 09 47 81 02 95 05 81 03 09 51 26 ff 00 75 10 95 01 81 02 35 00 65 11 55 0e 05 01 09 30 26 a0 44 46 96 14 81 42 09 31 26 9a 26 46 95 0b 81 42 05 0d 75 08 95 01 15 00 09 48 26 5f 00 46 7c 14 81 02 09 49 25 35 46 7d 0b 81 02 45 00 65 00 55 00 c0 05 0d 09 22 a1 02 05 0d 95 01 75 01 25 01 09 42 81 02 81 03 09 47 81 02 95 05 81 03 09 51 26 ff 00 75 10 95 01 81 02 35 00 65 11 55 0e 05 01 09 30 26 a0 44 46 96 14 81 42 09 31 26 9a 26 46 95 0b 81 42 05 0d 75 08 95 01 15 00 09 48 26 5f 00 46 7c 14 81 02 09 49 25 35 46 7d 0b 81 02 45 00 65 00 55 00 c0 05 0d 09 22 a1 02 05 0d 95 01 75 01 25 01 09 42 81 02 81 03 09 47 81 02 95 05 81 03 09 51 26 ff 00 75 10 95 01 81 02 35 00 65 11 55 0e 05 01 09 30 26 a0 44 46 96 14 81 42 09 31 26 9a 26 46 95 0b 81 42 05 0d 75 08 95 01 15 00 09 48 26 5f 00 46 7c 14 81 02 09 49 25 35 46 7d 0b 81 02 45 00 65 00 55 00 c0 05 0d 09 22 a1 02 05 0d 95 01 75 01 25 01 09 42 81 02 81 03 09 47 81 02 95 05 81 03 09 51 26 ff 00 75 10 95 01 81 02 35 00 65 11 55 0e 05 01 09 30 26 a0 44 46 96 14 81 42 09 31 26 9a 26 46 95 0b 81 42 05 0d 75 08 95 01 15 00 09 48 26 5f 00 46 7c 14 81 02 09 49 25 35 46 7d 0b 81 02 45 00 65 00 55 00 c0 05 0d 09 22 a1 02 05 0d 95 01 75 01 25 01 09 42 81 02 81 03 09 47 81 02 95 05 81 03 09 51 26 ff 00 75 10 95 01 81 02 35 00 65 11 55 0e 05 01 09 30 26 a0 44 46 96 14 81 42 09 31 26 9a 26 46 95 0b 81 42 05 0d 75 08 95 01 15 00 09 48 26 5f 00 46 7c 14 81 02 09 49 25 35 46 7d 0b 81 02 45 00 65 00 55 00 c0 05 0d 27 ff ff 00 00 75 10 95 01 09 56 81 02 75 08 95 0e 81 03 09 55 26 ff 00 75 08 b1 02 85 0a 06 00 ff 09 c5 96 00 01 b1 02 c0 06 00 ff 09 01 a1 01 09 01 85 13 15 00 26 ff 00 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0",
            input_info=(0x3, 0x056A, 0x0383),
        )

    def test_contact_id_0(self):
        """
        Bring a finger in contact with the tablet, then hold it down and remove it.

        Ensure that even with contact ID = 0 which is usually given as an invalid
        touch event by most tablets with the exception of a few, that given the
        confidence bit is set to 1 it should process it as a valid touch to cover
        the few tablets using contact ID = 0 as a valid touch value.
        """
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()

        t0 = test_multitouch.Touch(0, 50, 100)
        r = uhdev.event([t0])
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)

        slot = self.get_slot(uhdev, t0, 0)

        assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 1) in events
        assert evdev.slots[slot][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 0
        assert evdev.slots[slot][libevdev.EV_ABS.ABS_MT_POSITION_X] == 50
        assert evdev.slots[slot][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 100

        t0.tipswitch = False
        if uhdev.quirks is None or "VALID_IS_INRANGE" not in uhdev.quirks:
            t0.inrange = False
        r = uhdev.event([t0])
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)
        assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 0) in events
        assert evdev.slots[slot][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

    def test_confidence_false(self):
        """
        Bring a finger in contact with the tablet with confidence set to false.

        Ensure that the confidence bit being set to false should not result in a touch event.
        """
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()

        t0 = test_multitouch.Touch(1, 50, 100)
        t0.confidence = False
        r = uhdev.event([t0])
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)

        slot = self.get_slot(uhdev, t0, 0)

        assert not events