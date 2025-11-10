#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2017 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2017 Red Hat, Inc.
#

from . import base
from hidtools.hut import HUT
from hidtools.util import BusType
import libevdev
import logging
import pytest
import sys
import time

logger = logging.getLogger("hidtools.test.multitouch")

KERNEL_MODULE = base.KernelModule("hid-multitouch", "hid_multitouch")


def BIT(x):
    return 1 << x


mt_quirks = {
    "NOT_SEEN_MEANS_UP": BIT(0),
    "SLOT_IS_CONTACTID": BIT(1),
    "CYPRESS": BIT(2),
    "SLOT_IS_CONTACTNUMBER": BIT(3),
    "ALWAYS_VALID": BIT(4),
    "VALID_IS_INRANGE": BIT(5),
    "VALID_IS_CONFIDENCE": BIT(6),
    "CONFIDENCE": BIT(7),
    "SLOT_IS_CONTACTID_MINUS_ONE": BIT(8),
    "NO_AREA": BIT(9),
    "IGNORE_DUPLICATES": BIT(10),
    "HOVERING": BIT(11),
    "CONTACT_CNT_ACCURATE": BIT(12),
    "FORCE_GET_FEATURE": BIT(13),
    "FIX_CONST_CONTACT_ID": BIT(14),
    "TOUCH_SIZE_SCALING": BIT(15),
    "STICKY_FINGERS": BIT(16),
    "ASUS_CUSTOM_UP": BIT(17),
    "WIN8_PTP_BUTTONS": BIT(18),
    "SEPARATE_APP_REPORT": BIT(19),
    "MT_QUIRK_FORCE_MULTI_INPUT": BIT(20),
}


class Data(object):
    pass


class Touch(object):
    def __init__(self, id, x, y):
        self.contactid = id
        self.x = x
        self.y = y
        self.cx = x
        self.cy = y
        self.tipswitch = True
        self.confidence = True
        self.tippressure = 15
        self.azimuth = 0
        self.inrange = True
        self.width = 10
        self.height = 10


class Pen(Touch):
    def __init__(self, x, y):
        super().__init__(0, x, y)
        self.barrel = False
        self.invert = False
        self.eraser = False
        self.x_tilt = False
        self.y_tilt = False
        self.twist = 0


class Digitizer(base.UHIDTestDevice):
    @classmethod
    def msCertificationBlob(cls, reportID):
        return f"""
        Usage Page (Digitizers)
        Usage (Touch Screen)
        Collection (Application)
         Report ID ({reportID})
         Usage Page (0xff00)
         Usage (0xc5)
         Logical Minimum (0)
         Logical Maximum (255)
         Report Size (8)
         Report Count (256)
         Feature (Data,Var,Abs)
        End Collection
    """

    def __init__(
        self,
        name,
        rdesc_str=None,
        rdesc=None,
        application="Touch Screen",
        physical="Finger",
        max_contacts=None,
        input_info=(BusType.USB, 1, 2),
        quirks=None,
    ):
        super().__init__(name, application, rdesc_str, rdesc, input_info)
        self.scantime = 0
        self.quirks = quirks
        if max_contacts is None:
            self.max_contacts = sys.maxsize
            for features in self.parsed_rdesc.feature_reports.values():
                for feature in features:
                    if feature.usage_name in ["Contact Max"]:
                        self.max_contacts = feature.logical_max
            for inputs in self.parsed_rdesc.input_reports.values():
                for i in inputs:
                    if (
                        i.usage_name in ["Contact Count"]
                        and i.logical_max > 0
                        and self.max_contacts > i.logical_max
                    ):
                        self.max_contacts = i.logical_max
            if self.max_contacts == sys.maxsize:
                self.max_contacts = 1
        else:
            self.max_contacts = max_contacts
        self.physical = physical
        self.cur_application = application

        for features in self.parsed_rdesc.feature_reports.values():
            for feature in features:
                if feature.usage_name == "Inputmode":
                    self.cur_application = "Mouse"

        self.fields = []
        for r in self.parsed_rdesc.input_reports.values():
            if r.application_name == self.application:
                physicals = [f.physical_name for f in r]
                if self.physical not in physicals and None not in physicals:
                    continue
                self.fields = [f.usage_name for f in r]

    @property
    def touches_in_a_report(self):
        return self.fields.count("Contact Id")

    def event(self, slots, global_data=None, contact_count=None, incr_scantime=True):
        if incr_scantime:
            self.scantime += 1
        rs = []
        # make sure we have only the required number of available slots
        slots = slots[: self.max_contacts]

        if global_data is None:
            global_data = Data()
        if contact_count is None:
            global_data.contactcount = len(slots)
        else:
            global_data.contactcount = contact_count
        global_data.scantime = self.scantime

        while len(slots):
            r = self.create_report(
                application=self.cur_application, data=slots, global_data=global_data
            )
            self.call_input_event(r)
            rs.append(r)
            global_data.contactcount = 0
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

        if "Contact Max" not in [f.usage_name for f in rdesc]:
            return (1, [])

        self.contactmax = self.max_contacts
        r = rdesc.create_report([self], None)
        return (0, r)

    def set_report(self, req, rnum, rtype, data):
        if rtype != self.UHID_FEATURE_REPORT:
            return 1

        rdesc = None
        for v in self.parsed_rdesc.feature_reports.values():
            if v.report_ID == rnum:
                rdesc = v

        if rdesc is None:
            return 1

        if "Inputmode" not in [f.usage_name for f in rdesc]:
            return 0

        Inputmode_seen = False
        for f in rdesc:
            if "Inputmode" == f.usage_name:
                values = f.get_values(data)
                assert len(values) == 1
                value = values[0]

                if not Inputmode_seen:
                    Inputmode_seen = True
                    if value == 0:
                        self.cur_application = "Mouse"
                    elif value == 2:
                        self.cur_application = "Touch Screen"
                    elif value == 3:
                        self.cur_application = "Touch Pad"
                else:
                    if value != 0:
                        # Elan bug where the device doesn't work properly
                        # if we set twice an Input Mode in the same Feature
                        self.cur_application = "Mouse"

        return 0


class PTP(Digitizer):
    def __init__(
        self,
        name,
        type="Click Pad",
        rdesc_str=None,
        rdesc=None,
        application="Touch Pad",
        physical="Pointer",
        max_contacts=None,
        input_info=None,
    ):
        self.type = type.lower().replace(" ", "")
        if self.type == "clickpad":
            self.buttontype = 0
        else:  # pressurepad
            self.buttontype = 1
        self.clickpad_state = False
        self.left_state = False
        self.right_state = False
        super().__init__(
            name, rdesc_str, rdesc, application, physical, max_contacts, input_info
        )

    def event(
        self,
        slots=None,
        click=None,
        left=None,
        right=None,
        contact_count=None,
        incr_scantime=True,
    ):
        # update our internal state
        if click is not None:
            self.clickpad_state = click
        if left is not None:
            self.left_state = left
        if right is not None:
            self.right_state = right

        # now create the global data
        global_data = Data()
        global_data.b1 = 1 if self.clickpad_state else 0
        global_data.b2 = 1 if self.left_state else 0
        global_data.b3 = 1 if self.right_state else 0

        if slots is None:
            slots = [Data()]

        return super().event(slots, global_data, contact_count, incr_scantime)


class MinWin8TSParallel(Digitizer):
    def __init__(self, max_slots):
        self.max_slots = max_slots
        self.phys_max = 120, 90
        rdesc_finger_str = f"""
            Usage Page (Digitizers)
            Usage (Finger)
            Collection (Logical)
             Report Size (1)
             Report Count (1)
             Logical Minimum (0)
             Logical Maximum (1)
             Usage (Tip Switch)
             Input (Data,Var,Abs)
             Report Size (7)
             Logical Maximum (127)
             Input (Cnst,Var,Abs)
             Report Size (8)
             Logical Maximum (255)
             Usage (Contact Id)
             Input (Data,Var,Abs)
             Report Size (16)
             Unit Exponent (-1)
             Unit (SILinear: cm)
             Logical Maximum (4095)
             Physical Minimum (0)
             Physical Maximum ({self.phys_max[0]})
             Usage Page (Generic Desktop)
             Usage (X)
             Input (Data,Var,Abs)
             Physical Maximum ({self.phys_max[1]})
             Usage (Y)
             Input (Data,Var,Abs)
             Usage Page (Digitizers)
             Usage (Azimuth)
             Logical Maximum (360)
             Unit (SILinear: deg)
             Report Size (16)
             Input (Data,Var,Abs)
            End Collection
"""
        rdesc_str = f"""
           Usage Page (Digitizers)
           Usage (Touch Screen)
           Collection (Application)
            Report ID (1)
            {rdesc_finger_str * self.max_slots}
            Unit Exponent (-4)
            Unit (SILinear: s)
            Logical Maximum (65535)
            Physical Maximum (65535)
            Usage Page (Digitizers)
            Usage (Scan Time)
            Input (Data,Var,Abs)
            Report Size (8)
            Logical Maximum (255)
            Usage (Contact Count)
            Input (Data,Var,Abs)
            Report ID (2)
            Logical Maximum ({self.max_slots})
            Usage (Contact Max)
            Feature (Data,Var,Abs)
          End Collection
          {Digitizer.msCertificationBlob(68)}
"""
        super().__init__(f"uhid test parallel {self.max_slots}", rdesc_str)


class MinWin8TSHybrid(Digitizer):
    def __init__(self):
        self.max_slots = 10
        self.phys_max = 120, 90
        rdesc_finger_str = f"""
            Usage Page (Digitizers)
            Usage (Finger)
            Collection (Logical)
             Report Size (1)
             Report Count (1)
             Logical Minimum (0)
             Logical Maximum (1)
             Usage (Tip Switch)
             Input (Data,Var,Abs)
             Report Size (7)
             Logical Maximum (127)
             Input (Cnst,Var,Abs)
             Report Size (8)
             Logical Maximum (255)
             Usage (Contact Id)
             Input (Data,Var,Abs)
             Report Size (16)
             Unit Exponent (-1)
             Unit (SILinear: cm)
             Logical Maximum (4095)
             Physical Minimum (0)
             Physical Maximum ({self.phys_max[0]})
             Usage Page (Generic Desktop)
             Usage (X)
             Input (Data,Var,Abs)
             Physical Maximum ({self.phys_max[1]})
             Usage (Y)
             Input (Data,Var,Abs)
            End Collection
"""
        rdesc_str = f"""
           Usage Page (Digitizers)
           Usage (Touch Screen)
           Collection (Application)
            Report ID (1)
            {rdesc_finger_str * 2}
            Unit Exponent (-4)
            Unit (SILinear: s)
            Logical Maximum (65535)
            Physical Maximum (65535)
            Usage Page (Digitizers)
            Usage (Scan Time)
            Input (Data,Var,Abs)
            Report Size (8)
            Logical Maximum (255)
            Usage (Contact Count)
            Input (Data,Var,Abs)
            Report ID (2)
            Logical Maximum ({self.max_slots})
            Usage (Contact Max)
            Feature (Data,Var,Abs)
          End Collection
          {Digitizer.msCertificationBlob(68)}
"""
        super().__init__("uhid test hybrid", rdesc_str)


class Win8TSConfidence(Digitizer):
    def __init__(self, max_slots):
        self.max_slots = max_slots
        self.phys_max = 120, 90
        rdesc_finger_str = f"""
            Usage Page (Digitizers)
            Usage (Finger)
            Collection (Logical)
             Report Size (1)
             Report Count (1)
             Logical Minimum (0)
             Logical Maximum (1)
             Usage (Tip Switch)
             Input (Data,Var,Abs)
             Usage (Confidence)
             Input (Data,Var,Abs)
             Report Size (6)
             Logical Maximum (127)
             Input (Cnst,Var,Abs)
             Report Size (8)
             Logical Maximum (255)
             Usage (Contact Id)
             Input (Data,Var,Abs)
             Report Size (16)
             Unit Exponent (-1)
             Unit (SILinear: cm)
             Logical Maximum (4095)
             Physical Minimum (0)
             Physical Maximum ({self.phys_max[0]})
             Usage Page (Generic Desktop)
             Usage (X)
             Input (Data,Var,Abs)
             Physical Maximum ({self.phys_max[1]})
             Usage (Y)
             Input (Data,Var,Abs)
             Usage Page (Digitizers)
             Usage (Azimuth)
             Logical Maximum (360)
             Unit (SILinear: deg)
             Report Size (16)
             Input (Data,Var,Abs)
            End Collection
"""
        rdesc_str = f"""
           Usage Page (Digitizers)
           Usage (Touch Screen)
           Collection (Application)
            Report ID (1)
            {rdesc_finger_str * self.max_slots}
            Unit Exponent (-4)
            Unit (SILinear: s)
            Logical Maximum (65535)
            Physical Maximum (65535)
            Usage Page (Digitizers)
            Usage (Scan Time)
            Input (Data,Var,Abs)
            Report Size (8)
            Logical Maximum (255)
            Usage (Contact Count)
            Input (Data,Var,Abs)
            Report ID (2)
            Logical Maximum ({self.max_slots})
            Usage (Contact Max)
            Feature (Data,Var,Abs)
          End Collection
          {Digitizer.msCertificationBlob(68)}
"""
        super().__init__(f"uhid test confidence {self.max_slots}", rdesc_str)


class SmartTechDigitizer(Digitizer):
    def __init__(self, name, input_info):
        super().__init__(
            name,
            rdesc="05 01 09 02 a1 01 85 01 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 95 03 75 01 81 02 95 05 81 03 05 01 15 00 26 ff 0f 55 0e 65 11 75 10 95 01 35 00 46 c8 37 09 30 81 02 46 68 1f 09 31 81 02 45 00 c0 c0 05 0d 09 06 15 00 26 ff 00 a1 01 85 02 75 08 95 3f 09 00 82 02 01 95 3f 09 00 92 02 01 c0 05 0d 09 04 a1 01 85 05 05 0d 09 20 a1 00 25 01 75 01 95 02 09 42 09 45 81 02 75 06 95 01 09 30 81 02 26 ff 00 75 08 09 51 81 02 75 10 09 38 81 02 95 02 26 ff 0f 09 48 09 49 81 02 05 01 09 30 09 31 81 02 c0 05 0d 09 20 a1 00 25 01 75 01 95 02 09 42 09 45 81 02 75 06 95 01 09 30 81 02 26 ff 00 75 08 09 51 81 02 75 10 09 38 81 02 95 02 26 ff 0f 09 48 09 49 81 02 05 01 09 30 09 31 81 02 c0 05 0d 09 20 a1 00 25 01 75 01 95 02 09 42 09 45 81 02 75 06 95 01 09 30 81 02 26 ff 00 75 08 09 51 81 02 75 10 09 38 81 02 95 02 26 ff 0f 09 48 09 49 81 02 05 01 09 30 09 31 81 02 c0 05 0d 09 20 a1 00 25 01 75 01 95 02 09 42 09 45 81 02 75 06 95 01 09 30 81 02 26 ff 00 75 08 09 51 81 02 75 10 09 38 81 02 95 02 26 ff 0f 09 48 09 49 81 02 05 01 09 30 09 31 81 02 c0 05 0d 75 08 95 01 15 00 25 0a 09 54 81 02 09 55 b1 02 c0 05 0d 09 0e a1 01 85 04 09 23 a1 02 15 00 25 02 75 08 95 02 09 52 09 53 b1 02 c0 c0 05 0d 09 04 a1 01 85 03 05 0d 09 22 a1 02 15 00 25 01 75 01 95 02 09 42 09 47 81 02 95 02 81 03 75 04 95 01 25 0f 09 30 81 02 26 ff 00 75 08 95 01 09 51 81 02 75 10 27 a0 8c 00 00 55 0e 65 14 47 a0 8c 00 00 09 3f 81 02 65 11 26 ff 0f 46 c8 37 09 48 81 02 46 68 1f 09 49 81 02 05 01 46 c8 37 09 30 81 02 46 68 1f 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 15 00 25 01 75 01 95 02 09 42 09 47 81 02 95 02 81 03 75 04 95 01 25 0f 09 30 81 02 26 ff 00 75 08 95 01 09 51 81 02 75 10 27 a0 8c 00 00 55 0e 65 14 47 a0 8c 00 00 09 3f 81 02 65 11 26 ff 0f 46 c8 37 09 48 81 02 46 68 1f 09 49 81 02 05 01 46 c8 37 09 30 81 02 46 68 1f 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 15 00 25 01 75 01 95 02 09 42 09 47 81 02 95 02 81 03 75 04 95 01 25 0f 09 30 81 02 26 ff 00 75 08 95 01 09 51 81 02 75 10 27 a0 8c 00 00 55 0e 65 14 47 a0 8c 00 00 09 3f 81 02 65 11 26 ff 0f 46 c8 37 09 48 81 02 46 68 1f 09 49 81 02 05 01 46 c8 37 09 30 81 02 46 68 1f 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 15 00 25 01 75 01 95 02 09 42 09 47 81 02 95 02 81 03 75 04 95 01 25 0f 09 30 81 02 26 ff 00 75 08 95 01 09 51 81 02 75 10 27 a0 8c 00 00 55 0e 65 14 47 a0 8c 00 00 09 3f 81 02 65 11 26 ff 0f 46 c8 37 09 48 81 02 46 68 1f 09 49 81 02 05 01 46 c8 37 09 30 81 02 46 68 1f 09 31 81 02 45 00 c0 05 0d 75 08 95 01 15 00 25 0a 09 54 81 02 09 55 b1 02 c0 05 0d 09 04 a1 01 85 06 09 22 a1 02 15 00 25 01 75 01 95 02 09 42 09 47 81 02 95 06 81 03 95 01 75 10 65 11 55 0e 26 ff 0f 46 c8 37 09 48 81 02 46 68 1f 09 49 81 02 05 01 46 c8 37 09 30 81 02 46 68 1f 09 31 81 02 45 00 c0 c0 05 0d 09 02 a1 01 85 07 09 20 a1 02 25 01 75 01 95 04 09 42 09 44 09 3c 09 45 81 02 75 04 95 01 25 0f 09 30 81 02 26 ff 00 75 08 09 38 81 02 75 10 27 a0 8c 00 00 55 0e 65 14 47 a0 8c 00 00 09 3f 81 02 65 11 26 ff 0f 46 c8 37 09 48 81 02 46 68 1f 09 49 81 02 05 01 46 c8 37 09 30 81 02 46 68 1f 09 31 81 02 45 00 c0 c0 05 0d 09 02 a1 01 85 08 09 20 a1 02 25 01 75 01 95 04 09 42 09 44 09 3c 09 45 81 02 75 04 95 01 25 0f 09 30 81 02 26 ff 00 75 08 09 38 81 02 75 10 27 a0 8c 00 00 55 0e 65 14 47 a0 8c 00 00 09 3f 81 02 65 11 26 ff 0f 46 c8 37 09 48 81 02 46 68 1f 09 49 81 02 05 01 46 c8 37 09 30 81 02 46 68 1f 09 31 81 02 45 00 c0 c0 05 0d 09 02 a1 01 85 09 09 20 a1 02 25 01 75 01 95 04 09 42 09 44 09 3c 09 45 81 02 75 04 95 01 25 0f 09 30 81 02 26 ff 00 75 08 09 38 81 02 75 10 27 a0 8c 00 00 55 0e 65 14 47 a0 8c 00 00 09 3f 81 02 65 11 26 ff 0f 46 c8 37 09 48 81 02 46 68 1f 09 49 81 02 05 01 46 c8 37 09 30 81 02 46 68 1f 09 31 81 02 45 00 c0 c0 05 0d 09 02 a1 01 85 0a 09 20 a1 02 25 01 75 01 95 04 09 42 09 44 09 3c 09 45 81 02 75 04 95 01 25 0f 09 30 81 02 26 ff 00 75 08 09 38 81 02 75 10 27 a0 8c 00 00 55 0e 65 14 47 a0 8c 00 00 09 3f 81 02 65 11 26 ff 0f 46 c8 37 09 48 81 02 46 68 1f 09 49 81 02 05 01 46 c8 37 09 30 81 02 46 68 1f 09 31 81 02 45 00 c0 c0",
            input_info=input_info,
        )

    def create_report(self, data, global_data=None, reportID=None, application=None):
        # this device has *a lot* of different reports, and most of them
        # have the Touch Screen application. But the first one is a stylus
        # report (as stated in the physical type), so we simply override
        # the report ID to use what the device sends
        return super().create_report(data, global_data=global_data, reportID=3)

    def match_evdev_rule(self, application, evdev):
        # we need to select the correct evdev node, as the device has multiple
        # Touch Screen application collections
        if application != "Touch Screen":
            return True
        absinfo = evdev.absinfo[libevdev.EV_ABS.ABS_MT_POSITION_X]
        return absinfo is not None and absinfo.resolution == 3


class BaseTest:
    class TestMultitouch(base.BaseTestCase.TestUhid):
        kernel_modules = [KERNEL_MODULE]

        def create_device(self):
            raise Exception("please reimplement me in subclasses")

        def get_slot(self, uhdev, t, default):
            if uhdev.quirks is None:
                return default

            if "SLOT_IS_CONTACTID" in uhdev.quirks:
                return t.contactid

            if "SLOT_IS_CONTACTID_MINUS_ONE" in uhdev.quirks:
                return t.contactid - 1

            return default

        def test_creation(self):
            """Make sure the device gets processed by the kernel and creates
            the expected application input node.

            If this fail, there is something wrong in the device report
            descriptors."""
            super().test_creation()

            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            # some sanity checking for the quirks
            if uhdev.quirks is not None:
                for q in uhdev.quirks:
                    assert q in mt_quirks

            assert evdev.num_slots == uhdev.max_contacts

            if uhdev.max_contacts > 1:
                assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1
                assert evdev.slots[1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1
            if uhdev.max_contacts > 2:
                assert evdev.slots[2][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

        def test_required_usages(self):
            """Make sure the device exports the correct required features and
            inputs."""
            uhdev = self.uhdev
            rdesc = uhdev.parsed_rdesc
            for feature in rdesc.feature_reports.values():
                for field in feature:
                    page_id = field.usage >> 16
                    value = field.usage & 0xFF
                    try:
                        if HUT[page_id][value] == "Contact Max":
                            assert HUT[page_id][field.application] in [
                                "Touch Screen",
                                "Touch Pad",
                                "System Multi-Axis Controller",
                            ]
                    except KeyError:
                        pass

                    try:
                        if HUT[page_id][value] == "Inputmode":
                            assert HUT[page_id][field.application] in [
                                "Touch Screen",
                                "Touch Pad",
                                "Device Configuration",
                            ]
                    except KeyError:
                        pass

        def test_mt_single_touch(self):
            """send a single touch in the first slot of the device,
            and release it."""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            t0 = Touch(1, 50, 100)
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

        def test_mt_dual_touch(self):
            """Send 2 touches in the first 2 slots.
            Make sure the kernel sees this as a dual touch.
            Release and check

            Note: PTP will send here BTN_DOUBLETAP emulation"""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            t0 = Touch(1, 50, 100)
            t1 = Touch(2, 150, 200)

            if uhdev.quirks is not None and (
                "SLOT_IS_CONTACTID" in uhdev.quirks
                or "SLOT_IS_CONTACTNUMBER" in uhdev.quirks
            ):
                t1.contactid = 0

            slot0 = self.get_slot(uhdev, t0, 0)
            slot1 = self.get_slot(uhdev, t1, 1)

            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)

            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 1) in events
            assert evdev.value[libevdev.EV_KEY.BTN_TOUCH] == 1
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 0
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_POSITION_X] == 50
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 100
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

            r = uhdev.event([t0, t1])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH) not in events
            assert evdev.value[libevdev.EV_KEY.BTN_TOUCH] == 1
            assert (
                libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_POSITION_X, 5) not in events
            )
            assert (
                libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_POSITION_Y, 10) not in events
            )
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 0
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_POSITION_X] == 50
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 100
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 1
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_POSITION_X] == 150
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 200

            t0.tipswitch = False
            if uhdev.quirks is None or "VALID_IS_INRANGE" not in uhdev.quirks:
                t0.inrange = False
            r = uhdev.event([t0, t1])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 1
            assert libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_POSITION_X) not in events
            assert libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_POSITION_Y) not in events

            t1.tipswitch = False
            if uhdev.quirks is None or "VALID_IS_INRANGE" not in uhdev.quirks:
                t1.inrange = False

            if uhdev.quirks is not None and "SLOT_IS_CONTACTNUMBER" in uhdev.quirks:
                r = uhdev.event([t0, t1])
            else:
                r = uhdev.event([t1])

            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: uhdev.max_contacts <= 2, "Device not compatible"
        )
        def test_mt_triple_tap(self):
            """Send 3 touches in the first 3 slots.
            Make sure the kernel sees this as a triple touch.
            Release and check

            Note: PTP will send here BTN_TRIPLETAP emulation"""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            t0 = Touch(1, 50, 100)
            t1 = Touch(2, 150, 200)
            t2 = Touch(3, 250, 300)
            r = uhdev.event([t0, t1, t2])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)

            slot0 = self.get_slot(uhdev, t0, 0)
            slot1 = self.get_slot(uhdev, t1, 1)
            slot2 = self.get_slot(uhdev, t2, 2)

            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 0
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_POSITION_X] == 50
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 100
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 1
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_POSITION_X] == 150
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 200
            assert evdev.slots[slot2][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 2
            assert evdev.slots[slot2][libevdev.EV_ABS.ABS_MT_POSITION_X] == 250
            assert evdev.slots[slot2][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 300

            t0.tipswitch = False
            t1.tipswitch = False
            t2.tipswitch = False
            if uhdev.quirks is None or "VALID_IS_INRANGE" not in uhdev.quirks:
                t0.inrange = False
                t1.inrange = False
                t2.inrange = False
            r = uhdev.event([t0, t1, t2])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)

            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1
            assert evdev.slots[slot2][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: uhdev.max_contacts <= 2, "Device not compatible"
        )
        def test_mt_max_contact(self):
            """send the maximum number of contact as reported by the device.
            Make sure all contacts are forwarded and that there is no miss.
            Release and check."""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            touches = [
                Touch(i, (i + 3) * 20, (i + 3) * 20 + 5)
                for i in range(uhdev.max_contacts)
            ]
            if (
                uhdev.quirks is not None
                and "SLOT_IS_CONTACTID_MINUS_ONE" in uhdev.quirks
            ):
                for t in touches:
                    t.contactid += 1
            r = uhdev.event(touches)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            for i, t in enumerate(touches):
                slot = self.get_slot(uhdev, t, i)

                assert evdev.slots[slot][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == i
                assert evdev.slots[slot][libevdev.EV_ABS.ABS_MT_POSITION_X] == t.x
                assert evdev.slots[slot][libevdev.EV_ABS.ABS_MT_POSITION_Y] == t.y

            for t in touches:
                t.tipswitch = False
                if uhdev.quirks is None or "VALID_IS_INRANGE" not in uhdev.quirks:
                    t.inrange = False

            r = uhdev.event(touches)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            for i, t in enumerate(touches):
                slot = self.get_slot(uhdev, t, i)

                assert evdev.slots[slot][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: (
                uhdev.touches_in_a_report == 1
                or uhdev.quirks is not None
                and "CONTACT_CNT_ACCURATE" not in uhdev.quirks
            ),
            "Device not compatible, we can not trigger the conditions",
        )
        def test_mt_contact_count_accurate(self):
            """Test the MT_QUIRK_CONTACT_CNT_ACCURATE from the kernel.
            A report should forward an accurate contact count and the kernel
            should ignore any data provided after we have reached this
            contact count."""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            t0 = Touch(1, 50, 100)
            t1 = Touch(2, 150, 200)

            slot0 = self.get_slot(uhdev, t0, 0)
            slot1 = self.get_slot(uhdev, t1, 1)

            r = uhdev.event([t0, t1], contact_count=1)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 1) in events
            assert evdev.value[libevdev.EV_KEY.BTN_TOUCH] == 1
            assert libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_TRACKING_ID, 0) in events
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 0
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_POSITION_X] == 50
            assert evdev.slots[slot0][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 100
            assert evdev.slots[slot1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

    class TestWin8Multitouch(TestMultitouch):
        def test_required_usages8(self):
            """Make sure the device exports the correct required features and
            inputs."""
            uhdev = self.uhdev
            rdesc = uhdev.parsed_rdesc
            for feature in rdesc.feature_reports.values():
                for field in feature:
                    page_id = field.usage >> 16
                    value = field.usage & 0xFF
                    try:
                        if HUT[page_id][value] == "Inputmode":
                            assert HUT[field.application] not in ["Touch Screen"]
                    except KeyError:
                        pass

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: uhdev.fields.count("X") == uhdev.touches_in_a_report,
            "Device not compatible, we can not trigger the conditions",
        )
        def test_mt_tx_cx(self):
            """send a single touch in the first slot of the device, with
            different values of Tx and Cx. Make sure the kernel reports Tx."""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            t0 = Touch(1, 5, 10)
            t0.cx = 50
            t0.cy = 100
            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 1) in events
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 0
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_POSITION_X] == 5
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TOOL_X] == 50
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 10
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TOOL_Y] == 100

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: "In Range" not in uhdev.fields,
            "Device not compatible, missing In Range usage",
        )
        def test_mt_inrange(self):
            """Send one contact that has the InRange bit set before/after
            tipswitch.
            Kernel is supposed to mark the contact with a distance > 0
            when inrange is set but not tipswitch.

            This tests the hovering capability of devices (MT_QUIRK_HOVERING).

            Make sure the contact is only released from the kernel POV
            when the inrange bit is set to 0."""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            t0 = Touch(1, 150, 200)
            t0.tipswitch = False
            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 1) in events
            assert evdev.value[libevdev.EV_KEY.BTN_TOUCH] == 1
            assert libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_TRACKING_ID, 0) in events
            assert libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_DISTANCE) in events
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_DISTANCE] > 0
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 0
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_POSITION_X] == 150
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 200
            assert evdev.slots[1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

            t0.tipswitch = True
            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_DISTANCE, 0) in events
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_DISTANCE] == 0

            t0.tipswitch = False
            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_DISTANCE) in events
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_DISTANCE] > 0

            t0.inrange = False
            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 0) in events
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

        def test_mt_duplicates(self):
            """Test the MT_QUIRK_IGNORE_DUPLICATES from the kernel.
            If a touch is reported more than once with the same Contact ID,
            we should only handle the first touch.

            Note: this is not in MS spec, but the current kernel behaves
            like that"""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            t0 = Touch(1, 5, 10)
            t1 = Touch(1, 15, 20)
            t2 = Touch(2, 50, 100)

            r = uhdev.event([t0, t1, t2], contact_count=2)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 1) in events
            assert evdev.value[libevdev.EV_KEY.BTN_TOUCH] == 1
            assert libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_TRACKING_ID, 0) in events
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 0
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_POSITION_X] == 5
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 10
            assert evdev.slots[1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 1
            assert evdev.slots[1][libevdev.EV_ABS.ABS_MT_POSITION_X] == 50
            assert evdev.slots[1][libevdev.EV_ABS.ABS_MT_POSITION_Y] == 100

        def test_mt_release_miss(self):
            """send a single touch in the first slot of the device, and
            forget to release it. The kernel is supposed to release by itself
            the touch in 100ms.
            Make sure that we are dealing with a new touch by resending the
            same touch after the timeout expired, and check that the kernel
            considers it as a separate touch (different tracking ID)"""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            t0 = Touch(1, 5, 10)
            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 0

            time.sleep(0.2)
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 0) in events
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == 1

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: "Azimuth" not in uhdev.fields,
            "Device not compatible, missing Azimuth usage",
        )
        def test_mt_azimuth(self):
            """Check for the azimtuh information bit.
            When azimuth is presented by the device, it should be exported
            as ABS_MT_ORIENTATION and the exported value should report a quarter
            of circle."""
            uhdev = self.uhdev

            t0 = Touch(1, 5, 10)
            t0.azimuth = 270

            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)

            # orientation is clockwise, while Azimuth is counter clockwise
            assert libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_ORIENTATION, 90) in events

    class TestPTP(TestWin8Multitouch):
        def test_ptp_buttons(self):
            """check for button reliability.
            There are 2 types of touchpads: the click pads and the pressure pads.
            Each should reliably report the BTN_LEFT events.
            """
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            if uhdev.type == "clickpad":
                r = uhdev.event(click=True)
                events = uhdev.next_sync_events()
                self.debug_reports(r, uhdev, events)
                assert libevdev.InputEvent(libevdev.EV_KEY.BTN_LEFT, 1) in events
                assert evdev.value[libevdev.EV_KEY.BTN_LEFT] == 1

                r = uhdev.event(click=False)
                events = uhdev.next_sync_events()
                self.debug_reports(r, uhdev, events)
                assert libevdev.InputEvent(libevdev.EV_KEY.BTN_LEFT, 0) in events
                assert evdev.value[libevdev.EV_KEY.BTN_LEFT] == 0
            else:
                r = uhdev.event(left=True)
                events = uhdev.next_sync_events()
                self.debug_reports(r, uhdev, events)
                assert libevdev.InputEvent(libevdev.EV_KEY.BTN_LEFT, 1) in events
                assert evdev.value[libevdev.EV_KEY.BTN_LEFT] == 1

                r = uhdev.event(left=False)
                events = uhdev.next_sync_events()
                self.debug_reports(r, uhdev, events)
                assert libevdev.InputEvent(libevdev.EV_KEY.BTN_LEFT, 0) in events
                assert evdev.value[libevdev.EV_KEY.BTN_LEFT] == 0

                r = uhdev.event(right=True)
                events = uhdev.next_sync_events()
                self.debug_reports(r, uhdev, events)
                assert libevdev.InputEvent(libevdev.EV_KEY.BTN_RIGHT, 1) in events
                assert evdev.value[libevdev.EV_KEY.BTN_RIGHT] == 1

                r = uhdev.event(right=False)
                events = uhdev.next_sync_events()
                self.debug_reports(r, uhdev, events)
                assert libevdev.InputEvent(libevdev.EV_KEY.BTN_RIGHT, 0) in events
                assert evdev.value[libevdev.EV_KEY.BTN_RIGHT] == 0

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: "Confidence" not in uhdev.fields,
            "Device not compatible, missing Confidence usage",
        )
        def test_ptp_confidence(self):
            """Check for the validity of the confidence bit.
            When a contact is marked as not confident, it should be detected
            as a palm from the kernel POV and released.

            Note: if the kernel exports ABS_MT_TOOL_TYPE, it shouldn't release
            the touch but instead convert it to ABS_MT_TOOL_PALM."""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            t0 = Touch(1, 150, 200)
            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)

            t0.confidence = False
            r = uhdev.event([t0])
            events = uhdev.next_sync_events()
            self.debug_reports(r, uhdev, events)

            if evdev.absinfo[libevdev.EV_ABS.ABS_MT_TOOL_TYPE] is not None:
                # the kernel exports MT_TOOL_PALM
                assert (
                    libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_TOOL_TYPE, 2) in events
                )
                assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] != -1

                t0.tipswitch = False
                r = uhdev.event([t0])
                events = uhdev.next_sync_events()
                self.debug_reports(r, uhdev, events)

            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 0) in events
            assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1

        @pytest.mark.skip_if_uhdev(
            lambda uhdev: uhdev.touches_in_a_report >= uhdev.max_contacts,
            "Device not compatible, we can not trigger the conditions",
        )
        def test_ptp_non_touch_data(self):
            """Some single finger hybrid touchpads might not provide the
            button information in subsequent reports (only in the first report).

            Emulate this and make sure we do not release the buttons in the
            middle of the event."""
            uhdev = self.uhdev
            evdev = uhdev.get_evdev()

            touches = [Touch(i, i * 10, i * 10 + 5) for i in range(uhdev.max_contacts)]
            contact_count = uhdev.max_contacts
            incr_scantime = True
            btn_state = True
            events = None
            while touches:
                t = touches[: uhdev.touches_in_a_report]
                touches = touches[uhdev.touches_in_a_report :]
                r = uhdev.event(
                    t,
                    click=btn_state,
                    left=btn_state,
                    contact_count=contact_count,
                    incr_scantime=incr_scantime,
                )
                contact_count = 0
                incr_scantime = False
                btn_state = False
                events = uhdev.next_sync_events()
                self.debug_reports(r, uhdev, events)
                if touches:
                    assert len(events) == 0

            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_LEFT, 1) in events
            assert libevdev.InputEvent(libevdev.EV_KEY.BTN_LEFT, 0) not in events
            assert evdev.value[libevdev.EV_KEY.BTN_LEFT] == 1


################################################################################
#
# Windows 7 compatible devices
#
################################################################################
class Test3m_0596_0500(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test 3m_0596_0500",
            rdesc="05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 09 01 95 01 75 01 15 00 25 01 81 02 95 07 75 01 81 03 95 01 75 08 81 03 05 01 09 30 09 31 15 00 26 ff 7f 35 00 46 00 00 95 02 75 10 81 02 c0 a1 02 15 00 26 ff 00 09 01 95 39 75 08 81 01 c0 c0 05 0d 09 0e a1 01 85 11 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 09 04 a1 01 85 10 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 3a 06 81 02 09 31 46 e8 03 81 02 c0 05 0d a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 3a 06 81 02 09 31 46 e8 03 81 02 c0 05 0d a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 3a 06 81 02 09 31 46 e8 03 81 02 c0 05 0d a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 3a 06 81 02 09 31 46 e8 03 81 02 c0 05 0d a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 3a 06 81 02 09 31 46 e8 03 81 02 c0 05 0d a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 3a 06 81 02 09 31 46 e8 03 81 02 c0 05 0d a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 3a 06 81 02 09 31 46 e8 03 81 02 c0 05 0d a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 3a 06 81 02 09 31 46 e8 03 81 02 c0 05 0d a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 3a 06 81 02 09 31 46 e8 03 81 02 c0 05 0d a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 3a 06 81 02 09 31 46 e8 03 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 0a 81 02 85 12 09 55 95 01 75 08 15 00 25 0a b1 02 06 00 ff 15 00 26 ff 00 85 03 09 01 75 08 95 07 b1 02 85 04 09 01 75 08 95 17 b1 02 85 05 09 01 75 08 95 47 b1 02 85 06 09 01 75 08 95 07 b1 02 85 07 09 01 75 08 95 07 b1 02 85 08 09 01 75 08 95 07 b1 02 85 09 09 01 75 08 95 3f b1 02 c0",
            input_info=(BusType.USB, 0x0596, 0x0500),
            max_contacts=60,
            quirks=("VALID_IS_CONFIDENCE", "SLOT_IS_CONTACTID", "TOUCH_SIZE_SCALING"),
        )


class Test3m_0596_0506(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test 3m_0596_0506",
            rdesc="05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 09 01 95 01 75 01 15 00 25 01 81 02 95 07 75 01 81 03 95 01 75 08 81 03 05 01 09 30 09 31 15 00 26 ff 7f 35 00 46 00 00 95 02 75 10 81 02 c0 a1 02 15 00 26 ff 00 09 01 95 39 75 08 81 03 c0 c0 05 0d 09 0e a1 01 85 11 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 09 04 a1 01 85 13 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 d6 0a 81 02 09 31 46 22 06 81 02 05 0d 75 10 95 01 09 48 81 02 09 49 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 d6 0a 81 02 09 31 46 22 06 81 02 05 0d 75 10 95 01 09 48 81 02 09 49 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 d6 0a 81 02 09 31 46 22 06 81 02 05 0d 75 10 95 01 09 48 81 02 09 49 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 d6 0a 81 02 09 31 46 22 06 81 02 05 0d 75 10 95 01 09 48 81 02 09 49 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 d6 0a 81 02 09 31 46 22 06 81 02 05 0d 75 10 95 01 09 48 81 02 09 49 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 d6 0a 81 02 09 31 46 22 06 81 02 05 0d 75 10 95 01 09 48 81 02 09 49 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 3c 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 02 81 03 05 0d 85 12 09 55 95 01 75 08 15 00 25 3c b1 02 06 00 ff 15 00 26 ff 00 85 03 09 01 75 08 95 07 b1 02 85 04 09 01 75 08 95 17 b1 02 85 05 09 01 75 08 95 47 b1 02 85 06 09 01 75 08 95 07 b1 02 85 73 09 01 75 08 95 07 b1 02 85 08 09 01 75 08 95 07 b1 02 85 09 09 01 75 08 95 3f b1 02 85 0f 09 01 75 08 96 07 02 b1 02 c0",
            input_info=(BusType.USB, 0x0596, 0x0506),
            max_contacts=60,
            quirks=("VALID_IS_CONFIDENCE", "SLOT_IS_CONTACTID", "TOUCH_SIZE_SCALING"),
        )


class TestActionStar_2101_1011(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test ActionStar_2101_1011",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 26 ff 4d 46 70 03 81 02 09 31 26 ff 2b 46 f1 01 81 02 46 00 00 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 26 ff 4d 46 70 03 81 02 09 31 26 ff 2b 46 f1 01 81 02 46 00 00 c0 05 0d 09 54 75 08 95 01 81 02 05 0d 85 02 09 55 25 02 75 08 95 01 b1 02 c0",
            input_info=(BusType.USB, 0x2101, 0x1011),
        )

    def test_mt_actionstar_inrange(self):
        """Special sequence that might not be handled properly"""
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()

        # fmt: off
        sequence = [
            # t0 = Touch(1, 6999, 2441) | t1 = Touch(2, 15227, 2026)
            '01 ff 01 57 1b 89 09 ff 02 7b 3b ea 07 02',
            # t0.xy = (6996, 2450)      | t1.y = 2028
            '01 ff 01 54 1b 92 09 ff 02 7b 3b ec 07 02',
            # t1.xy = (15233, 2040)     | t0.tipswitch = False
            '01 ff 02 81 3b f8 07 fe 01 54 1b 92 09 02',
            # t1                        | t0.inrange = False
            '01 ff 02 81 3b f8 07 fc 01 54 1b 92 09 02',
        ]
        # fmt: on

        for num, r_str in enumerate(sequence):
            r = [int(i, 16) for i in r_str.split()]
            uhdev.call_input_event(r)
            events = uhdev.next_sync_events()
            self.debug_reports([r], uhdev)
            for e in events:
                print(e)
            if num == 2:
                assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1


class TestAsus_computers_0486_0185(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test asus-computers_0486_0185",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 95 01 75 01 81 02 09 32 81 02 09 47 81 02 75 05 81 03 09 30 26 ff 00 75 08 81 02 09 51 25 02 81 02 26 96 0d 05 01 75 10 55 0d 65 33 09 30 35 00 46 fd 1d 81 02 09 31 46 60 11 81 02 c0 09 22 a1 02 05 0d 35 00 45 00 55 00 65 00 09 42 25 01 75 01 81 02 09 32 81 02 09 47 81 02 75 05 81 03 09 30 26 ff 00 75 08 81 02 09 51 25 02 81 02 26 96 0d 05 01 75 10 55 0d 65 33 09 30 46 fd 1d 81 02 09 31 46 60 11 81 02 c0 35 00 45 00 55 00 65 00 05 0d 09 54 75 08 25 02 81 02 85 08 09 55 b1 02 c0 09 0e a1 01 85 07 09 22 a1 00 09 52 25 0a b1 02 c0 05 0c 09 01 a1 01 85 06 09 01 26 ff 00 95 08 b1 02 c0 c0 05 01 09 02 a1 01 85 03 09 01 a1 00 05 09 19 01 29 02 25 01 75 01 95 02 81 02 95 06 81 03 26 96 0d 05 01 75 10 95 01 55 0d 65 33 09 30 46 fd 1d 81 02 09 31 46 60 11 81 02 c0 c0 06 ff 01 09 01 a1 01 26 ff 00 35 00 45 00 55 00 65 00 85 05 75 08 95 3f 09 00 81 02 c0",
            input_info=(BusType.USB, 0x0486, 0x0185),
            quirks=("VALID_IS_CONFIDENCE", "SLOT_IS_CONTACTID_MINUS_ONE"),
        )


class TestAtmel_03eb_201c(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test atmel_03eb_201c",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 26 ff 4b 46 70 03 81 02 09 31 26 ff 2b 46 f1 01 81 02 46 00 00 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 26 ff 4b 46 70 03 81 02 09 31 26 ff 2b 46 f1 01 81 02 46 00 00 c0 05 0d 09 54 75 08 95 01 81 02 05 0d 85 02 09 55 25 02 75 08 95 01 b1 02 c0",
            input_info=(BusType.USB, 0x03EB, 0x201C),
        )


class TestAtmel_03eb_211c(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test atmel_03eb_211c",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 37 81 02 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 46 56 0a 26 ff 0f 09 30 81 02 46 b2 05 26 ff 0f 09 31 81 02 05 0d 75 08 85 02 09 55 25 10 b1 02 c0 c0",
            input_info=(BusType.USB, 0x03EB, 0x211C),
        )


class TestCando_2087_0a02(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test cando_2087_0a02",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 0f 75 10 55 0e 65 33 09 30 35 00 46 6d 03 81 02 46 ec 01 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 0f 75 10 55 0e 65 33 09 30 35 00 46 6d 03 81 02 46 ec 01 09 31 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 02 81 02 85 02 09 55 b1 02 c0 06 00 ff 09 01 a1 01 85 a6 95 22 75 08 26 ff 00 15 00 09 01 81 02 85 a5 95 06 75 08 26 ff 00 15 00 09 01 91 02 c0",
            input_info=(BusType.USB, 0x2087, 0x0A02),
        )


class TestCando_2087_0b03(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test cando_2087_0b03",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 26 ff 49 46 f2 03 81 02 09 31 26 ff 29 46 39 02 81 02 46 00 00 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 26 ff 49 46 f2 03 81 02 09 31 26 ff 29 46 39 02 81 02 46 00 00 c0 05 0d 09 54 75 08 95 01 81 02 05 0d 85 02 09 55 25 02 75 08 95 01 b1 02 c0",
            input_info=(BusType.USB, 0x2087, 0x0B03),
        )


class TestCVTouch_1ff7_0013(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test cvtouch_1ff7_0013",
            rdesc="06 00 ff 09 00 a1 01 85 fd 06 00 ff 09 01 09 02 09 03 09 04 09 05 09 06 15 00 26 ff 00 75 08 95 06 81 02 85 fe 06 00 ff 09 01 09 02 09 03 09 04 15 00 26 ff 00 75 08 95 04 b1 02 c0 05 01 09 02 a1 01 09 01 a1 00 85 01 05 09 19 01 29 03 15 00 25 01 95 03 75 01 81 02 95 01 75 05 81 03 05 01 09 30 09 31 15 00 26 ff 7f 35 00 46 ff 7f 75 10 95 02 81 02 05 0d 09 33 15 00 26 ff 00 35 00 46 ff 00 75 08 95 01 81 02 05 01 09 38 15 81 25 7f 35 81 45 7f 95 01 81 06 c0 c0 06 00 ff 09 00 a1 01 85 fc 15 00 26 ff 00 19 01 29 3f 75 08 95 3f 81 02 19 01 29 3f 91 02 c0 06 00 ff 09 00 a1 01 85 fb 15 00 26 ff 00 19 01 29 3f 75 08 95 3f 81 02 19 01 29 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 81 02 c0 05 0d 09 54 15 00 26 ff 00 95 01 75 08 81 02 85 03 09 55 15 00 25 02 b1 02 c0 09 0e a1 01 85 04 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x1FF7, 0x0013),
            quirks=("NOT_SEEN_MEANS_UP",),
        )


class TestCvtouch_1ff7_0017(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test cvtouch_1ff7_0017",
            rdesc="06 00 ff 09 00 a1 01 85 fd 06 00 ff 09 01 09 02 09 03 09 04 09 05 09 06 15 00 26 ff 00 75 08 95 06 81 02 85 fe 06 00 ff 09 01 09 02 09 03 09 04 15 00 26 ff 00 75 08 95 04 b1 02 c0 05 01 09 02 a1 01 09 01 a1 00 85 01 05 09 19 01 29 03 15 00 25 01 95 03 75 01 81 02 95 01 75 05 81 03 05 01 09 30 09 31 15 00 26 ff 0f 35 00 46 ff 0f 75 10 95 02 81 02 09 00 15 00 25 ff 35 00 45 ff 75 08 95 01 81 02 09 38 15 81 25 7f 95 01 81 06 c0 c0 06 00 ff 09 00 a1 01 85 fc 15 00 25 ff 19 01 29 3f 75 08 95 3f 81 02 19 01 29 3f 91 02 c0 06 00 ff 09 00 a1 01 85 fb 15 00 25 ff 19 01 29 3f 75 08 95 3f 81 02 19 01 29 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 0f 75 10 55 00 65 00 09 30 35 00 46 ff 0f 81 02 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 0f 75 10 55 00 65 00 09 30 35 00 46 ff 0f 81 02 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 0f 75 10 55 00 65 00 09 30 35 00 46 ff 0f 81 02 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 0f 75 10 55 00 65 00 09 30 35 00 46 ff 0f 81 02 09 31 81 02 c0 05 0d 09 54 95 01 75 08 81 02 85 03 09 55 25 02 b1 02 c0 09 0e a1 01 85 04 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x1FF7, 0x0017),
        )


class TestCypress_04b4_c001(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test cypress_04b4_c001",
            rdesc="05 01 09 02 a1 01 85 01 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 95 03 75 01 81 02 95 01 75 05 81 01 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0 05 0d 09 04 a1 01 85 02 09 22 09 53 95 01 75 08 81 02 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 15 00 25 20 09 48 81 02 09 49 81 02 05 01 15 00 26 d0 07 75 10 55 00 65 00 09 30 15 00 26 d0 07 35 00 45 00 81 02 09 31 45 00 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 15 00 25 20 09 48 81 02 09 49 81 02 05 01 15 00 26 d0 07 75 10 55 00 65 00 09 30 15 00 26 d0 07 35 00 45 00 81 02 09 31 45 00 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 15 00 25 20 09 48 81 02 09 49 81 02 05 01 15 00 26 d0 07 75 10 55 00 65 00 09 30 15 00 26 d0 07 35 00 45 00 81 02 09 31 45 00 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 15 00 25 20 09 48 81 02 09 49 81 02 05 01 15 00 26 d0 07 75 10 55 00 65 00 09 30 15 00 26 d0 07 35 00 45 00 81 02 09 31 45 00 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 15 00 25 20 09 48 81 02 09 49 81 02 05 01 15 00 26 d0 07 75 10 55 00 65 00 09 30 15 00 26 d0 07 35 00 45 00 81 02 09 31 45 00 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 15 00 25 20 09 48 81 02 09 49 81 02 05 01 15 00 26 d0 07 75 10 55 00 65 00 09 30 15 00 26 d0 07 35 00 45 00 81 02 09 31 45 00 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 0a 81 02 09 55 b1 02 c0 09 0e a1 01 85 03 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x04B4, 0xC001),
        )


class TestData_modul_7374_1232(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test data-modul_7374_1232",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 37 81 02 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 46 d0 07 26 ff 0f 09 30 81 02 46 40 06 09 31 81 02 05 0d 75 08 85 02 09 55 25 10 b1 02 c0 c0",
            input_info=(BusType.USB, 0x7374, 0x1232),
        )


class TestData_modul_7374_1252(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test data-modul_7374_1252",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 37 81 02 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 46 d0 07 26 ff 0f 09 30 81 02 46 40 06 09 31 81 02 05 0d 75 08 85 02 09 55 25 10 b1 02 c0 c0",
            input_info=(BusType.USB, 0x7374, 0x1252),
        )


class TestE4_2219_044c(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test e4_2219_044c",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 08 81 02 09 55 b1 02 c0 09 0e a1 01 85 02 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 85 03 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 95 03 75 01 81 02 95 01 75 05 81 01 05 01 09 30 09 31 15 00 26 ff 7f 75 10 95 02 81 02 05 01 09 38 15 81 25 7f 75 08 95 01 81 06 c0 c0",
            input_info=(BusType.USB, 0x2219, 0x044C),
        )


class TestEgalax_capacitive_0eef_7224(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test egalax-capacitive_0eef_7224",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 34 49 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 37 29 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 34 49 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 37 29 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x0EEF, 0x7224),
            quirks=("SLOT_IS_CONTACTID", "ALWAYS_VALID"),
        )


class TestEgalax_capacitive_0eef_72fa(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test egalax-capacitive_0eef_72fa",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 72 22 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 87 13 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 72 22 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 87 13 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x0EEF, 0x72FA),
            quirks=("SLOT_IS_CONTACTID", "VALID_IS_INRANGE"),
        )


class TestEgalax_capacitive_0eef_7336(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test egalax-capacitive_0eef_7336",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 c1 20 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 c2 18 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 c1 20 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 c2 18 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x0EEF, 0x7336),
        )


class TestEgalax_capacitive_0eef_7337(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test egalax-capacitive_0eef_7337",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 ae 17 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 c3 0e 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 ae 17 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 c3 0e 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x0EEF, 0x7337),
        )


class TestEgalax_capacitive_0eef_7349(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test egalax-capacitive_0eef_7349",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 34 49 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 37 29 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 34 49 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 37 29 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x0EEF, 0x7349),
            quirks=("SLOT_IS_CONTACTID", "ALWAYS_VALID"),
        )


class TestEgalax_capacitive_0eef_73f4(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test egalax-capacitive_0eef_73f4",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 96 4e 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 23 2c 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 04 a1 01 85 02 09 20 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 75 01 81 03 05 01 09 30 75 10 95 01 a4 55 0d 65 33 36 00 00 46 96 4e 16 00 00 26 ff 0f 81 02 09 31 16 00 00 26 ff 0f 36 00 00 46 23 2c 81 02 b4 c0 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x0EEF, 0x73F4),
        )


class TestEgalax_capacitive_0eef_a001(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test egalax-capacitive_0eef_a001",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 15 00 25 01 81 02 09 51 75 05 95 01 16 00 00 26 10 00 81 02 09 47 75 01 95 01 15 00 25 01 81 02 05 01 09 30 75 10 95 01 55 0d 65 33 35 00 46 23 28 26 ff 7f 81 02 09 31 75 10 95 01 55 0d 65 33 35 00 46 11 19 26 ff 7f 81 02 05 0d 09 55 25 08 75 08 95 01 b1 02 c0 c0 05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 0e a1 01 85 05 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x0EEF, 0xA001),
            quirks=("SLOT_IS_CONTACTID", "VALID_IS_INRANGE"),
        )


class TestElo_touchsystems_04e7_0022(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test elo-touchsystems_04e7_0022",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 0f 75 10 55 0e 65 33 09 30 35 00 46 ff 0f 81 02 46 ff 0f 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 0f 75 10 55 00 65 00 09 30 35 00 46 ff 0f 81 02 46 ff 0f 09 31 81 02 c0 05 0d 09 54 25 10 95 01 75 08 81 02 85 08 09 55 25 02 b1 02 c0 09 0e a1 01 85 07 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 06 00 ff 09 55 85 80 15 00 26 ff 00 75 08 95 01 b1 82 c0 05 01 09 02 a1 01 85 54 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 15 00 26 ff 0f 75 10 95 01 81 02 09 31 75 10 95 01 81 02 09 3b 16 00 00 26 00 01 36 00 00 46 00 01 66 00 00 75 10 95 01 81 62 c0 c0",
            input_info=(BusType.USB, 0x04E7, 0x0022),
        )


class TestElo_touchsystems_04e7_0080(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test elo-touchsystems_04e7_0080",
            rdesc="05 0d 09 04 a1 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 09 47 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 46 7c 24 75 10 95 01 09 30 81 02 09 31 46 96 14 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 09 47 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 46 7c 24 75 10 95 01 09 30 81 02 09 31 46 96 14 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 09 47 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 46 7c 24 75 10 95 01 09 30 81 02 09 31 46 96 14 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 09 47 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 46 7c 24 75 10 95 01 09 30 81 02 09 31 46 96 14 81 02 c0 05 0d 09 54 75 08 95 01 15 00 25 08 81 02 09 55 b1 02 c0",
            input_info=(BusType.USB, 0x04E7, 0x0080),
        )


class TestFlatfrog_25b5_0002(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test flatfrog_25b5_0002",
            rdesc="05 0d 09 04 a1 01 85 05 09 22 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 05 0d 09 22 65 00 55 00 a1 02 05 0d 15 00 25 01 75 01 95 01 09 42 81 02 09 32 81 02 95 06 81 03 75 08 95 01 25 7f 09 51 81 02 05 01 65 11 55 0e 75 10 35 00 26 a6 2b 46 48 1b 09 30 81 02 26 90 18 46 59 0f 09 31 81 02 05 0d 65 11 55 0f 75 08 25 7f 45 7f 09 48 81 02 09 49 81 02 65 00 55 00 75 10 26 00 04 46 00 04 09 30 81 02 c0 65 00 55 00 05 0d 55 0c 66 01 10 75 20 95 01 27 ff ff ff 7f 45 00 09 56 81 02 75 08 95 01 15 00 25 28 09 54 81 02 09 55 85 06 25 28 b1 02 c0 65 00 55 00 45 00 09 0e a1 01 85 03 09 23 a1 02 09 52 15 02 25 02 75 08 95 01 b1 02 09 53 15 00 25 0a 75 08 95 01 b1 02 c0 c0",
            input_info=(BusType.USB, 0x25B5, 0x0002),
            quirks=("NOT_SEEN_MEANS_UP", "NO_AREA"),
            max_contacts=40,
        )


class TestFocaltech_10c4_81b9(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test focaltech_10c4_81b9",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 04 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 26 58 02 09 31 46 00 00 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 04 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 26 58 02 09 31 46 00 00 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 04 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 26 58 02 09 31 46 00 00 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 04 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 26 58 02 09 31 46 00 00 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 04 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 26 58 02 09 31 46 00 00 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 08 81 02 85 02 09 55 75 08 95 01 b1 02 c0",
            input_info=(BusType.USB, 0x10C4, 0x81B9),
            quirks=("ALWAYS_VALID",),
            max_contacts=5,
        )


class TestHanvon_20b3_0a18(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test hanvon_20b3_0a18",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 26 ff 4b 46 70 03 81 02 09 31 26 ff 2b 46 f1 01 81 02 46 00 00 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 26 ff 4b 46 70 03 81 02 09 31 26 ff 2b 46 f1 01 81 02 46 00 00 c0 05 0d 09 54 75 08 95 01 81 02 05 0d 85 02 09 55 25 02 75 08 95 01 b1 02 c0",
            input_info=(BusType.USB, 0x20B3, 0x0A18),
        )


class TestHuitoo_03f7_0003(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test huitoo_03f7_0003",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 00 65 00 35 00 46 ff 0f 09 30 26 ff 0f 81 02 09 31 26 ff 0f 81 02 05 0d 09 48 26 ff 0f 81 02 09 49 26 ff 0f 81 02 c0 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 00 65 00 35 00 46 ff 0f 09 30 26 ff 0f 81 02 09 31 26 ff 0f 81 02 05 0d 09 48 26 ff 0f 81 02 09 49 26 ff 0f 81 02 c0 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 00 65 00 35 00 46 ff 0f 09 30 26 ff 0f 81 02 09 31 26 ff 0f 81 02 05 0d 09 48 26 ff 0f 81 02 09 49 26 ff 0f 81 02 c0 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 00 65 00 35 00 46 ff 0f 09 30 26 ff 0f 81 02 09 31 26 ff 0f 81 02 05 0d 09 48 26 ff 0f 81 02 09 49 26 ff 0f 81 02 c0 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 00 65 00 35 00 46 ff 0f 09 30 26 ff 0f 81 02 09 31 26 ff 0f 81 02 05 0d 09 48 26 ff 0f 81 02 09 49 26 ff 0f 81 02 c0 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 00 65 00 35 00 46 ff 0f 09 30 26 ff 0f 81 02 09 31 26 ff 0f 81 02 05 0d 09 48 26 ff 0f 81 02 09 49 26 ff 0f 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 08 81 02 09 55 b1 02 c0 09 0e a1 01 85 02 09 23 a1 02 09 52 09 53 15 00 25 10 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 85 03 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 95 03 75 01 81 02 95 01 75 05 81 01 05 01 09 30 09 31 15 00 26 ff 0f 35 00 46 ff 0f 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 85 04 15 00 26 ff 00 75 08 95 3f 09 02 81 02 95 3f 09 02 91 02 c0",
            input_info=(BusType.USB, 0x03F7, 0x0003),
        )


class TestIdeacom_1cb6_6650(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test ideacom_1cb6_6650",
            rdesc="05 0d 09 04 a1 01 85 0a 09 22 a1 00 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 81 03 05 01 26 ff 1f 75 10 95 01 55 0d 65 33 09 31 35 00 46 61 13 81 02 09 30 46 73 22 81 02 05 0d 75 08 95 01 09 30 26 ff 00 81 02 09 51 81 02 85 0c 09 55 25 02 95 01 b1 02 c0 06 00 ff 85 02 09 01 75 08 95 07 b1 02 85 03 09 02 75 08 95 07 b1 02 85 04 09 03 75 08 95 07 b1 02 85 05 09 04 75 08 95 07 b1 02 85 06 09 05 75 08 96 27 00 b1 02 85 07 09 06 75 08 96 27 00 b1 02 85 08 09 07 75 08 95 07 b1 02 85 09 09 08 75 08 95 07 b1 02 85 0b 09 09 75 08 96 07 00 b1 02 85 0d 09 0a 75 08 96 27 00 b1 02 c0 09 0e a1 01 85 0e 09 52 09 53 95 07 b1 02 c0 05 01 09 02 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 75 06 95 01 81 01 05 01 09 31 09 30 15 00 27 ff 1f 00 00 75 10 95 02 81 02 c0 09 01 a1 02 15 00 26 ff 00 95 02 75 08 81 03 c0 c0",
            input_info=(BusType.USB, 0x1CB6, 0x6650),
        )


class TestIdeacom_1cb6_6651(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test ideacom_1cb6_6651",
            rdesc="05 0d 09 04 a1 01 85 0a 09 22 a1 02 09 42 09 32 15 00 25 01 95 02 75 01 81 02 95 06 81 03 05 01 26 ff 1f 75 10 95 01 55 0d 65 33 09 31 35 00 46 39 13 81 02 09 30 46 24 22 81 02 05 0d 75 08 95 01 09 30 26 ff 00 81 02 09 51 81 02 85 0c 09 55 25 02 95 01 b1 02 c0 06 00 ff 85 02 09 01 75 08 95 07 b1 02 85 03 09 02 75 08 95 07 b1 02 85 04 09 03 75 08 95 07 b1 02 85 05 09 04 75 08 95 07 b1 02 85 06 09 05 75 08 95 1f b1 02 85 07 09 06 75 08 96 1f 00 b1 02 85 08 09 07 75 08 95 07 b1 02 85 09 09 08 75 08 95 07 b1 02 85 0b 09 09 75 08 95 07 b1 02 85 0d 09 0a 75 08 96 1f 00 b1 02 c0 09 0e a1 01 85 0e 09 52 09 53 95 07 b1 02 c0 05 01 09 02 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 75 06 95 01 81 01 05 01 09 31 09 30 15 00 27 ff 1f 00 00 75 10 95 02 81 02 c0 09 01 a1 02 15 00 26 ff 00 95 02 75 08 81 03 c0 c0",
            input_info=(BusType.USB, 0x1CB6, 0x6651),
        )


class TestIkaist_2793_0001(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test ikaist_2793_0001",
            rdesc="05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 09 01 95 01 75 01 15 00 25 01 81 02 95 07 75 01 81 03 95 01 75 08 81 03 05 01 09 30 09 31 15 00 26 ff 7f 35 00 46 00 00 95 02 75 10 81 02 c0 a1 02 15 00 26 ff 00 09 01 95 39 75 08 81 03 c0 c0 05 0d 09 0e a1 01 85 11 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 09 04 a1 01 85 13 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 51 07 81 02 09 31 46 96 04 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 51 07 81 02 09 31 46 96 04 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 51 07 81 02 09 31 46 96 04 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 51 07 81 02 09 31 46 96 04 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 51 07 81 02 09 31 46 96 04 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 51 07 81 02 09 31 46 96 04 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 51 07 81 02 09 31 46 96 04 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 51 07 81 02 09 31 46 96 04 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 51 07 81 02 09 31 46 96 04 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 33 09 30 35 00 46 51 07 81 02 09 31 46 96 04 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 3c 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 02 81 03 05 0d 85 12 09 55 95 01 75 08 15 00 25 3c b1 02 06 00 ff 15 00 26 ff 00 85 1e 09 01 75 08 95 80 b1 02 85 1f 09 01 75 08 96 3f 01 b1 02 c0",
            input_info=(BusType.USB, 0x2793, 0x0001),
        )


class TestIrmtouch_23c9_5666(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test irmtouch_23c9_5666",
            rdesc="05 0d 09 04 a1 01 85 0a 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 09 30 81 02 09 31 81 02 05 0d 09 48 09 49 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 09 30 81 02 09 31 81 02 05 0d 09 48 09 49 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 09 30 81 02 09 31 81 02 05 0d 09 48 09 49 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 09 30 81 02 09 31 81 02 05 0d 09 48 09 49 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 09 30 81 02 09 31 81 02 05 0d 09 48 09 49 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 15 00 26 ff 7f 75 10 09 30 81 02 09 31 81 02 05 0d 09 48 09 49 95 02 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 55 25 06 b1 02 c0 09 0e a1 01 85 0c 09 23 a1 02 09 52 15 00 25 06 75 08 95 01 b1 02 c0 c0",
            input_info=(BusType.USB, 0x23C9, 0x5666),
        )


class TestIrtouch_6615_0070(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test irtouch_6615_0070",
            rdesc="05 01 09 02 a1 01 85 10 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 06 81 03 05 01 09 30 09 31 15 00 26 ff 7f 75 10 95 02 81 02 c0 c0 05 0d 09 04 a1 01 85 30 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 09 30 26 ff 7f 55 0f 65 11 35 00 46 51 02 75 10 95 01 81 02 09 31 35 00 46 73 01 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 09 30 26 ff 7f 55 0f 65 11 35 00 46 51 02 75 10 95 01 81 02 09 31 35 00 46 73 01 81 02 c0 05 0d 09 54 15 00 26 02 00 75 08 95 01 81 02 85 03 09 55 15 00 26 ff 00 75 08 95 01 b1 02 c0 05 0d 09 0e a1 01 85 02 09 52 09 53 15 00 26 ff 00 75 08 95 02 b1 02 c0 05 0d 09 02 a1 01 85 20 09 20 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 05 01 09 30 26 ff 7f 55 0f 65 11 35 00 46 51 02 75 10 95 01 81 02 09 31 35 00 46 73 01 81 02 85 01 06 00 ff 09 01 75 08 95 01 b1 02 c0 c0",
            input_info=(BusType.USB, 0x6615, 0x0070),
        )


class TestIrtouch_6615_0081(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test irtouch_6615_0081",
            rdesc="05 0d 09 04 a1 01 85 30 09 22 09 00 15 00 26 ff 00 75 08 95 05 81 02 a1 00 05 0d 09 51 15 00 26 ff 00 75 08 95 01 81 02 05 01 09 30 26 ff 7f 55 0e 65 13 35 00 46 b5 04 75 10 95 01 81 02 09 31 35 00 46 8a 03 81 02 09 32 35 00 46 8a 03 81 02 09 00 15 00 26 ff 7f 75 10 95 01 81 02 09 00 15 00 26 ff 7f 75 10 95 01 81 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 00 15 00 26 ff 00 75 08 95 01 81 02 c0 a1 00 05 0d 09 51 15 00 26 ff 00 75 08 95 01 81 02 05 01 09 30 26 ff 7f 55 0e 65 13 35 00 46 b5 04 75 10 95 01 81 02 09 31 35 00 46 8a 03 81 02 09 32 35 00 46 8a 03 81 02 09 00 15 00 26 ff 7f 75 10 95 01 81 02 09 00 15 00 26 ff 7f 75 10 95 01 81 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 00 15 00 26 ff 00 75 08 95 01 81 02 c0 a1 00 05 0d 09 51 15 00 26 ff 00 75 08 95 01 81 02 05 01 09 30 26 ff 7f 55 0e 65 13 35 00 46 b5 04 75 10 95 01 81 02 09 31 35 00 46 8a 03 81 02 09 32 35 00 46 8a 03 81 02 09 00 15 00 26 ff 7f 75 10 95 01 81 02 09 00 15 00 26 ff 7f 75 10 95 01 81 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 00 15 00 26 ff 00 75 08 95 01 81 02 c0 a1 00 05 0d 09 54 15 00 25 1f 75 05 95 01 81 02 09 00 15 00 25 07 75 03 95 01 81 02 09 00 15 00 26 ff 00 75 08 95 01 81 02 c0 09 55 85 03 15 00 26 ff 00 75 08 95 01 b1 02 c0 05 0d 09 0e a1 01 85 02 09 52 09 53 15 00 26 ff 00 75 08 95 02 b1 02 c0 06 00 ff 09 00 a1 01 09 02 a1 00 85 aa 09 06 15 00 26 ff 00 35 00 46 ff 00 75 08 95 3f b1 02 c0 c0 05 01 09 02 a1 01 85 10 09 01 a1 00 05 01 09 00 15 00 26 ff 00 75 08 95 05 81 02 09 30 09 31 09 32 15 00 26 ff 7f 75 10 95 03 81 02 05 09 19 01 29 08 15 00 25 01 95 08 75 01 81 02 09 00 15 00 26 ff 00 75 08 95 02 81 02 c0 c0 06 00 ff 09 00 a1 01 85 40 09 00 15 00 26 ff 00 75 08 95 2e 81 02 c0",
            input_info=(BusType.USB, 0x6615, 0x0081),
        )


class TestLG_043e_9aa1(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test lg_043e_9aa1",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 09 31 46 78 0a 26 38 04 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 0a 81 02 25 0a 09 55 b1 02 c0 09 0e a1 01 85 03 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 85 04 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 75 10 95 01 15 00 26 7f 07 81 02 09 31 26 37 04 81 02 c0 c0 06 00 ff 09 01 a1 01 85 05 15 00 26 ff 00 75 08 95 19 09 01 b1 02 c0 05 14 09 2b a1 02 85 07 09 2b 15 00 25 0a 75 08 95 40 b1 02 09 4b 15 00 25 0a 75 08 95 02 91 02 c0 05 14 09 2c a1 02 85 08 09 2b 15 00 25 0a 75 08 95 05 81 02 09 4b 15 00 25 0a 75 08 95 47 91 02 c0",
            input_info=(BusType.USB, 0x043E, 0x9AA1),
        )


class TestLG_043e_9aa3(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test lg_043e_9aa3",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 09 31 46 78 0a 26 38 04 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 0a 81 02 25 0a 09 55 b1 02 c0 09 0e a1 01 85 03 09 22 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 85 04 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 75 10 95 01 15 00 26 7f 07 81 02 09 31 26 37 04 81 02 c0 c0 06 00 ff 09 01 a1 01 85 05 15 00 26 ff 00 75 08 95 19 09 01 b1 02 c0 05 14 09 2b a1 02 85 07 09 2b 15 00 25 0a 75 08 95 40 b1 02 09 4b 15 00 25 0a 75 08 95 02 91 02 c0 05 14 09 2c a1 02 85 08 09 2b 15 00 25 0a 75 08 95 05 81 02 09 4b 15 00 25 0a 75 08 95 47 91 02 c0",
            input_info=(BusType.USB, 0x043E, 0x9AA3),
        )


class TestLG_1fd2_0064(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test lg_1fd2_0064",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 a1 00 05 01 26 80 07 75 10 55 0e 65 33 09 30 35 00 46 53 07 81 02 26 38 04 46 20 04 09 31 81 02 45 00 c0 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 a1 00 05 01 26 80 07 75 10 55 0e 65 33 09 30 35 00 46 53 07 81 02 26 38 04 46 20 04 09 31 81 02 45 00 c0 c0 05 0d 09 54 95 01 75 08 81 02 85 08 09 55 95 01 25 02 b1 02 c0 09 0e a1 01 85 07 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 85 03 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 75 10 95 02 15 00 26 ff 7f 81 02 c0 c0",
            input_info=(BusType.USB, 0x1FD2, 0x0064),
        )


class TestLumio_202e_0006(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test lumio_202e_0006",
            rdesc="05 0d 09 04 a1 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 09 47 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 46 b0 0e 75 10 95 01 09 30 81 02 09 31 46 c2 0b 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 09 47 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 46 b0 0e 75 10 95 01 09 30 81 02 09 31 46 c2 0b 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 09 47 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 46 b0 0e 75 10 95 01 09 30 81 02 09 31 46 c2 0b 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 09 47 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 46 b0 0e 75 10 95 01 09 30 81 02 09 31 46 c2 0b 81 02 c0 05 0d 09 54 75 08 95 01 15 00 25 08 81 02 09 55 b1 02 c0",
            input_info=(BusType.USB, 0x202E, 0x0006),
            quirks=("VALID_IS_CONFIDENCE", "SLOT_IS_CONTACTID_MINUS_ONE"),
        )


class TestLumio_202e_0007(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test lumio_202e_0007",
            rdesc="05 0d 09 04 a1 01 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 09 47 95 02 81 02 95 0a 81 03 05 01 26 ff 7f 65 11 55 0e 46 ba 0e 75 10 95 01 09 30 81 02 09 31 46 ea 0b 81 02 05 0d 09 51 75 10 95 01 81 02 09 55 15 00 25 08 75 08 95 01 b1 02 c0 c0",
            input_info=(BusType.USB, 0x202E, 0x0007),
            quirks=("VALID_IS_CONFIDENCE", "SLOT_IS_CONTACTID_MINUS_ONE"),
        )


class TestNexio_1870_0100(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test nexio_1870_0100",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 05 0d 09 54 95 01 75 08 25 02 81 02 85 02 09 55 25 02 b1 02 c0 09 0e a1 01 85 03 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 04 05 09 95 03 75 01 19 01 29 03 15 00 25 01 81 02 95 01 75 05 81 01 05 01 75 10 95 02 09 30 09 31 15 00 26 ff 7f 81 02 c0 c0 05 0d 09 02 a1 01 85 05 09 20 a1 00 09 42 09 32 15 00 25 01 75 01 95 02 81 02 95 0e 81 03 05 01 26 ff 3f 75 10 95 01 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 c0 06 00 ff 09 01 a1 01 85 06 19 01 29 40 15 00 26 ff 00 75 08 95 40 81 00 19 01 29 40 91 00 c0",
            input_info=(BusType.USB, 0x1870, 0x0100),
        )


class TestNexio_1870_010d(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test nexio_1870_010d",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 05 0d 09 54 95 01 75 08 25 02 81 02 85 02 09 55 25 06 b1 02 c0 09 0e a1 01 85 03 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 04 05 09 95 03 75 01 19 01 29 03 15 00 25 01 81 02 95 01 75 05 81 01 05 01 75 10 95 02 09 30 09 31 15 00 26 ff 7f 81 02 c0 c0 05 0d 09 02 a1 01 85 05 09 20 a1 00 09 42 09 32 15 00 25 01 75 01 95 02 81 02 95 0e 81 03 05 01 26 ff 3f 75 10 95 01 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 c0 06 00 ff 09 01 a1 01 85 06 19 01 29 40 15 00 26 ff 00 75 08 95 3e 81 00 19 01 29 40 91 00 c0",
            input_info=(BusType.USB, 0x1870, 0x010D),
        )


class TestNexio_1870_0119(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test nexio_1870_0119",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0d 65 00 09 30 35 00 46 00 00 81 02 26 ff 3f 09 31 35 00 46 00 00 81 02 26 ff 3f 05 0d 09 48 35 00 26 ff 3f 81 02 09 49 35 00 26 ff 3f 81 02 c0 05 0d 09 54 95 01 75 08 25 02 81 02 85 02 09 55 25 06 b1 02 c0 09 0e a1 01 85 03 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 04 05 09 95 03 75 01 19 01 29 03 15 00 25 01 81 02 95 01 75 05 81 01 05 01 75 10 95 02 09 30 09 31 15 00 26 ff 7f 81 02 c0 c0 05 0d 09 02 a1 01 85 05 09 20 a1 00 09 42 09 32 15 00 25 01 75 01 95 02 81 02 95 0e 81 03 05 01 26 ff 3f 75 10 95 01 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 c0 06 00 ff 09 01 a1 01 85 06 19 01 29 40 15 00 26 ff 00 75 08 95 3e 81 00 19 01 29 40 91 00 c0",
            input_info=(BusType.USB, 0x1870, 0x0119),
        )


class TestPenmount_14e1_3500(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test penmount_14e1_3500",
            rdesc="05 0d 09 04 a1 01 09 22 a1 00 09 51 15 00 25 0f 75 04 95 01 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 81 01 05 01 75 10 95 01 09 30 26 ff 07 81 02 09 31 26 ff 07 81 02 05 0d 09 55 75 08 95 05 b1 02 c0 c0",
            input_info=(BusType.USB, 0x14E1, 0x3500),
            quirks=("VALID_IS_CONFIDENCE",),
            max_contacts=10,
        )


class TestPixart_093a_8002(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test pixart_093a_8002",
            rdesc="05 01 09 02 a1 01 85 0d 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 03 05 01 55 0e 65 11 75 10 95 01 35 00 46 5a 14 26 ff 7f 09 30 81 22 46 72 0b 26 ff 7f 09 31 81 22 95 08 75 08 81 03 c0 c0 05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 5a 14 26 ff 7f 81 02 09 31 46 72 0b 26 ff 7f 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 5a 14 26 ff 7f 81 02 46 72 0b 26 ff 7f 09 31 81 02 c0 05 0d 09 54 15 00 26 ff 00 95 01 75 08 81 02 09 55 25 02 95 01 85 02 b1 02 c0 05 0d 09 0e a1 01 06 00 ff 09 01 26 ff 00 75 08 95 47 85 03 b1 02 09 01 96 ff 03 85 04 b1 02 09 01 95 0b 85 05 b1 02 09 01 96 ff 03 85 06 b1 02 09 01 95 0f 85 07 b1 02 09 01 96 ff 03 85 08 b1 02 09 01 96 ff 03 85 09 b1 02 09 01 95 3f 85 0a b1 02 09 01 96 ff 03 85 0b b1 02 09 01 96 c3 03 85 0e b1 02 09 01 96 ff 03 85 0f b1 02 09 01 96 83 03 85 10 b1 02 09 01 96 93 00 85 11 b1 02 09 01 96 ff 03 85 12 b1 02 05 0d 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 85 0c b1 02 c0 c0",
            input_info=(BusType.USB, 0x093A, 0x8002),
            quirks=("VALID_IS_INRANGE", "SLOT_IS_CONTACTNUMBER"),
        )


class TestPqlabs_1ef1_0001(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test pqlabs_1ef1_0001",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 0e 65 11 09 30 35 00 46 1e 19 81 02 26 ff 3f 09 31 35 00 46 be 0f 81 02 26 ff 3f c0 05 0d 09 54 95 01 75 08 25 02 81 02 85 02 09 55 25 02 b1 02 c0 09 0e a1 01 85 03 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 04 05 09 95 03 75 01 19 01 29 03 15 00 25 01 81 02 95 01 75 05 81 01 05 01 75 10 95 02 09 30 09 31 15 00 26 ff 3f 81 02 c0 c0 05 8c 09 07 a1 01 85 11 09 02 15 00 26 ff 00 75 08 95 3f 81 02 85 10 09 10 91 02 c0",
            input_info=(BusType.USB, 0x1EF1, 0x0001),
        )


class TestQuanta_0408_3000(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test quanta_0408_3000",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 e3 13 26 7f 07 81 02 09 31 46 2f 0b 26 37 04 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 e3 13 26 7f 07 81 02 46 2f 0b 26 37 04 09 31 81 02 c0 05 0d 09 54 15 00 26 ff 00 95 01 75 08 81 02 09 55 25 02 95 01 85 02 b1 02 06 00 ff 09 01 26 ff 00 75 08 95 2f 85 03 b1 02 09 01 96 ff 03 85 04 b1 02 09 01 95 0b 85 05 b1 02 09 01 96 ff 03 85 06 b1 02 c0",
            input_info=(BusType.USB, 0x0408, 0x3000),
        )


class TestQuanta_0408_3001(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test quanta_0408_3001",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 09 31 46 78 0a 26 38 04 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 80 07 81 02 46 78 0a 26 38 04 09 31 81 02 c0 05 0d 09 54 15 00 26 ff 00 95 01 75 08 81 02 09 55 25 02 95 01 85 02 b1 02 06 00 ff 09 01 26 ff 00 75 08 95 47 85 03 b1 02 09 01 96 ff 03 85 04 b1 02 09 01 95 0b 85 05 b1 02 09 01 96 ff 03 85 06 b1 02 09 01 95 0f 85 07 b1 02 09 01 96 ff 03 85 08 b1 02 09 01 96 ff 03 85 09 b1 02 09 01 95 0f 85 0a b1 02 09 01 96 ff 03 85 0b b1 02 c0",
            input_info=(BusType.USB, 0x0408, 0x3001),
            quirks=("VALID_IS_CONFIDENCE", "SLOT_IS_CONTACTID"),
        )


class TestQuanta_0408_3008_1(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test quanta_0408_3008_1",
            rdesc="05 01 09 02 a1 01 85 0d 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 03 05 01 55 0e 65 11 75 10 95 01 35 00 46 4c 11 26 7f 07 09 30 81 22 46 bb 09 26 37 04 09 31 81 22 95 08 75 08 81 03 c0 c0 05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 4c 11 26 7f 07 81 02 09 31 46 bb 09 26 37 04 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 4c 11 26 7f 07 81 02 46 bb 09 26 37 04 09 31 81 02 c0 05 0d 09 54 15 00 26 ff 00 95 01 75 08 81 02 09 55 25 02 95 01 85 02 b1 02 c0 05 0d 09 0e a1 01 06 00 ff 09 01 26 ff 00 75 08 95 47 85 03 b1 02 09 01 96 ff 03 85 04 b1 02 09 01 95 0b 85 05 b1 02 09 01 96 ff 03 85 06 b1 02 09 01 95 0f 85 07 b1 02 09 01 96 ff 03 85 08 b1 02 09 01 96 ff 03 85 09 b1 02 09 01 95 3f 85 0a b1 02 09 01 96 ff 03 85 0b b1 02 09 01 96 c3 03 85 0e b1 02 09 01 96 ff 03 85 0f b1 02 09 01 96 83 03 85 10 b1 02 09 01 96 93 00 85 11 b1 02 09 01 96 ff 03 85 12 b1 02 05 0d 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 85 0c b1 02 c0 c0",
            input_info=(BusType.USB, 0x0408, 0x3008),
        )


class TestQuanta_0408_3008(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test quanta_0408_3008",
            rdesc="05 01 09 02 a1 01 85 0d 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 03 05 01 55 0e 65 11 75 10 95 01 35 00 46 98 12 26 7f 07 09 30 81 22 46 78 0a 26 37 04 09 31 81 22 c0 c0 05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 7f 07 81 02 09 31 46 78 0a 26 37 04 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 7f 07 81 02 46 78 0a 26 37 04 09 31 81 02 c0 05 0d 09 54 15 00 26 ff 00 95 01 75 08 81 02 09 55 25 02 95 01 85 02 b1 02 c0 05 0d 09 0e a1 01 06 00 ff 09 01 26 ff 00 75 08 95 47 85 03 b1 02 09 01 96 ff 03 85 04 b1 02 09 01 95 0b 85 05 b1 02 09 01 96 ff 03 85 06 b1 02 09 01 95 0f 85 07 b1 02 09 01 96 ff 03 85 08 b1 02 09 01 96 ff 03 85 09 b1 02 09 01 95 3f 85 0a b1 02 09 01 96 ff 03 85 0b b1 02 09 01 96 c3 03 85 0e b1 02 09 01 96 ff 03 85 0f b1 02 09 01 96 83 03 85 10 b1 02 09 01 96 93 00 85 11 b1 02 09 01 96 ff 03 85 12 b1 02 05 0d 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 85 0c b1 02 c0 c0",
            input_info=(BusType.USB, 0x0408, 0x3008),
        )


class TestRafi_05bd_0107(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test rafi_05bd_0107",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 65 00 55 00 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 25 09 95 01 81 02 05 01 46 9c 01 26 ff 03 35 00 75 10 09 30 81 02 46 e7 00 26 ff 03 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 25 09 95 01 81 02 05 01 46 9c 01 26 ff 03 35 00 75 10 09 30 81 02 46 e7 00 26 ff 03 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 25 09 95 01 81 02 05 01 46 9c 01 26 ff 03 35 00 75 10 09 30 81 02 46 e7 00 26 ff 03 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 25 09 95 01 81 02 05 01 46 9c 01 26 ff 03 35 00 75 10 09 30 81 02 46 e7 00 26 ff 03 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 25 09 95 01 81 02 05 01 46 9c 01 26 ff 03 35 00 75 10 09 30 81 02 46 e7 00 26 ff 03 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 25 09 95 01 81 02 05 01 46 9c 01 26 ff 03 35 00 75 10 09 30 81 02 46 e7 00 26 ff 03 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 25 09 95 01 81 02 05 01 46 9c 01 26 ff 03 35 00 75 10 09 30 81 02 46 e7 00 26 ff 03 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 25 09 95 01 81 02 05 01 46 9c 01 26 ff 03 35 00 75 10 09 30 81 02 46 e7 00 26 ff 03 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 25 09 95 01 81 02 05 01 46 9c 01 26 ff 03 35 00 75 10 09 30 81 02 46 e7 00 26 ff 03 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 25 09 95 01 81 02 05 01 46 9c 01 26 ff 03 35 00 75 10 09 30 81 02 46 e7 00 26 ff 03 09 31 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 09 81 02 05 0d 85 02 95 01 75 08 09 55 25 0a b1 02 c0 09 0e a1 01 85 03 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 05 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 06 81 03 05 01 65 11 55 0f 09 30 26 ff 03 35 00 46 9c 01 75 10 95 01 81 02 09 31 26 ff 03 35 00 46 e7 00 81 02 c0 c0",
            input_info=(BusType.USB, 0x05BD, 0x0107),
        )


class TestRndplus_2512_5003(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test rndplus_2512_5003",
            rdesc="05 0d 09 04 a1 01 85 02 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 08 81 02 85 08 09 55 b1 02 c0 09 0e a1 01 85 07 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 85 03 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 95 03 75 01 81 02 95 01 75 05 81 01 05 01 09 30 09 31 16 00 00 26 ff 3f 36 00 00 46 ff 3f 66 00 00 75 10 95 02 81 62 c0 c0",
            input_info=(BusType.USB, 0x2512, 0x5003),
        )


class TestRndplus_2512_5004(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test rndplus_2512_5004",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 3f 75 10 55 00 65 00 09 30 35 00 46 00 00 81 02 09 31 46 00 00 81 02 05 0d 09 48 09 49 75 10 95 02 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 08 81 02 85 05 09 55 b1 02 c0 09 0e a1 01 85 06 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 85 03 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 95 03 75 01 81 02 95 01 75 05 81 01 05 01 09 30 09 31 16 00 00 26 ff 3f 36 00 00 46 ff 3f 66 00 00 75 10 95 02 81 62 c0 c0 06 00 ff 09 01 a1 01 85 01 09 01 15 00 26 ff 00 75 08 95 3f 82 00 01 85 02 09 01 15 00 26 ff 00 75 08 95 3f 92 00 01 c0",
            input_info=(BusType.USB, 0x2512, 0x5004),
        )


class TestSitronix_1403_5001(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test sitronix_1403_5001",
            rdesc="05 0d 09 04 a1 01 85 01 09 54 95 01 75 08 81 02 09 22 a1 02 09 51 75 06 95 01 81 02 09 42 09 32 15 00 25 01 75 01 95 02 81 02 05 01 26 90 04 75 0c 95 01 55 0f 65 11 a4 09 30 46 e1 00 81 02 26 50 03 09 31 45 7d 81 02 05 0d 75 08 95 02 09 48 09 49 81 02 c0 a1 02 09 51 75 06 95 01 81 02 09 42 09 32 15 00 25 01 75 01 95 02 81 02 b4 a4 09 30 46 e1 00 81 02 26 50 03 09 31 45 7d 81 02 05 0d 75 08 95 02 09 48 09 49 81 02 c0 a1 02 09 51 75 06 95 01 81 02 09 42 09 32 15 00 25 01 75 01 95 02 81 02 b4 a4 09 30 46 e1 00 81 02 26 50 03 09 31 45 7d 81 02 05 0d 75 08 95 02 09 48 09 49 81 02 c0 a1 02 09 51 75 06 95 01 81 02 09 42 09 32 15 00 25 01 75 01 95 02 81 02 b4 a4 09 30 46 e1 00 81 02 26 50 03 09 31 45 7d 81 02 05 0d 75 08 95 02 09 48 09 49 81 02 c0 a1 02 09 51 75 06 95 01 81 02 09 42 09 32 15 00 25 01 75 01 95 02 81 02 b4 a4 09 30 46 e1 00 81 02 26 50 03 09 31 45 7d 81 02 05 0d 75 08 95 02 09 48 09 49 81 02 c0 a1 02 09 51 75 06 95 01 81 02 09 42 09 32 15 00 25 01 75 01 95 02 81 02 b4 a4 09 30 46 e1 00 81 02 26 50 03 09 31 45 7d 81 02 05 0d 75 08 95 02 09 48 09 49 81 02 c0 a1 02 09 51 75 06 95 01 81 02 09 42 09 32 15 00 25 01 75 01 95 02 81 02 b4 a4 09 30 46 e1 00 81 02 26 50 03 09 31 45 7d 81 02 05 0d 75 08 95 02 09 48 09 49 81 02 c0 a1 02 09 51 75 06 95 01 81 02 09 42 09 32 15 00 25 01 75 01 95 02 81 02 b4 a4 09 30 46 e1 00 81 02 26 50 03 09 31 45 7d 81 02 05 0d 75 08 95 02 09 48 09 49 81 02 c0 a1 02 09 51 75 06 95 01 81 02 09 42 09 32 15 00 25 01 75 01 95 02 81 02 b4 a4 09 30 46 e1 00 81 02 26 50 03 09 31 45 7d 81 02 05 0d 75 08 95 02 09 48 09 49 81 02 c0 a1 02 09 51 75 06 95 01 81 02 09 42 09 32 15 00 25 01 75 01 95 02 81 02 b4 09 30 46 e1 00 81 02 26 50 03 09 31 45 7d 81 02 05 0d 75 08 95 04 09 48 09 49 81 02 c0 85 02 09 55 26 ff 00 75 08 95 01 b1 02 09 04 15 00 25 ff 75 08 95 07 91 02 c0 09 0e a1 01 85 03 09 23 a1 00 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
            input_info=(BusType.USB, 0x1403, 0x5001),
            max_contacts=10,
        )


class TestSmart_0b8c_0092(BaseTest.TestMultitouch):
    def create_device(self):
        return SmartTechDigitizer(
            "uhid test smart_0b8c_0092", input_info=(BusType.USB, 0x0B8C, 0x0092)
        )


class TestStantum_1f87_0002(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test stantum_1f87_0002",
            rdesc="05 0d 09 04 a1 01 85 03 05 0d 09 54 95 01 75 08 81 02 06 00 ff 75 02 09 01 81 01 75 0e 09 02 81 02 05 0d 09 22 a1 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 30 81 02 05 0d 25 1f 75 05 09 48 81 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 31 81 02 05 0d 25 1f 75 05 09 49 81 02 75 08 09 51 95 01 81 02 09 30 75 05 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 47 81 02 09 32 81 02 c0 a1 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 30 81 02 05 0d 25 1f 75 05 09 48 81 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 31 81 02 05 0d 25 1f 75 05 09 49 81 02 75 08 09 51 95 01 81 02 09 30 75 05 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 47 81 02 09 32 81 02 c0 a1 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 30 81 02 05 0d 25 1f 75 05 09 48 81 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 31 81 02 05 0d 25 1f 75 05 09 49 81 02 75 08 09 51 95 01 81 02 09 30 75 05 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 47 81 02 09 32 81 02 c0 a1 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 30 81 02 05 0d 25 1f 75 05 09 48 81 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 31 81 02 05 0d 25 1f 75 05 09 49 81 02 75 08 09 51 95 01 81 02 09 30 75 05 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 47 81 02 09 32 81 02 c0 a1 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 30 81 02 05 0d 25 1f 75 05 09 48 81 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 31 81 02 05 0d 25 1f 75 05 09 49 81 02 75 08 09 51 95 01 81 02 09 30 75 05 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 47 81 02 09 32 81 02 c0 a1 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 30 81 02 05 0d 25 1f 75 05 09 48 81 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 31 81 02 05 0d 25 1f 75 05 09 49 81 02 75 08 09 51 95 01 81 02 09 30 75 05 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 47 81 02 09 32 81 02 c0 a1 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 30 81 02 05 0d 25 1f 75 05 09 48 81 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 31 81 02 05 0d 25 1f 75 05 09 49 81 02 75 08 09 51 95 01 81 02 09 30 75 05 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 47 81 02 09 32 81 02 c0 a1 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 30 81 02 05 0d 25 1f 75 05 09 48 81 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 31 81 02 05 0d 25 1f 75 05 09 49 81 02 75 08 09 51 95 01 81 02 09 30 75 05 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 47 81 02 09 32 81 02 c0 a1 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 30 81 02 05 0d 25 1f 75 05 09 48 81 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 31 81 02 05 0d 25 1f 75 05 09 49 81 02 75 08 09 51 95 01 81 02 09 30 75 05 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 47 81 02 09 32 81 02 c0 a1 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 30 81 02 05 0d 25 1f 75 05 09 48 81 02 05 01 16 00 00 26 ff 07 75 0b 55 00 65 00 09 31 81 02 05 0d 25 1f 75 05 09 49 81 02 75 08 09 51 95 01 81 02 09 30 75 05 81 02 09 42 15 00 25 01 75 01 95 01 81 02 09 47 81 02 09 32 81 02 c0 85 08 05 0d 09 55 95 01 75 08 25 0a b1 02 c0",
            input_info=(BusType.USB, 0x1F87, 0x0002),
        )


class TestTopseed_1784_0016(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test topseed_1784_0016",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 04 75 10 55 00 65 00 09 30 35 00 46 ff 04 81 02 09 31 46 ff 04 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 0a 81 02 09 55 b1 02 c0 05 0c 09 01 a1 01 85 03 a1 02 09 b5 15 00 25 01 75 01 95 01 81 02 09 b6 81 02 09 b7 81 02 09 cd 81 02 09 e2 81 02 09 e9 81 02 09 ea 81 02 05 01 09 82 81 02 c0 c0",
            input_info=(BusType.USB, 0x1784, 0x0016),
            max_contacts=2,
        )


class TestTpv_25aa_8883(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test tpv_25aa_8883",
            rdesc="05 01 09 02 a1 01 85 0d 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 05 0d 09 32 95 01 75 01 81 02 95 01 75 05 81 03 05 01 55 0e 65 11 75 10 95 01 35 00 46 98 12 26 7f 07 09 30 81 22 46 78 0a 26 37 04 09 31 81 22 35 00 45 00 15 81 25 7f 75 08 95 01 09 38 81 06 09 00 75 08 95 07 81 03 c0 c0 05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 7f 07 81 02 09 31 46 78 0a 26 37 04 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 75 10 55 0e 65 11 09 30 35 00 46 98 12 26 7f 07 81 02 46 78 0a 26 37 04 09 31 81 02 c0 05 0d 09 54 15 00 26 ff 00 95 01 75 08 81 02 09 55 25 02 95 01 85 02 b1 02 c0 05 0d 09 0e a1 01 06 00 ff 09 01 26 ff 00 75 08 95 47 85 03 b1 02 09 01 96 ff 03 85 04 b1 02 09 01 95 0b 85 05 b1 02 09 01 96 ff 03 85 06 b1 02 09 01 95 0f 85 07 b1 02 09 01 96 ff 03 85 08 b1 02 09 01 96 ff 03 85 09 b1 02 09 01 95 3f 85 0a b1 02 09 01 96 ff 03 85 0b b1 02 09 01 96 c3 03 85 0e b1 02 09 01 96 ff 03 85 0f b1 02 09 01 96 83 03 85 10 b1 02 09 01 96 93 00 85 11 b1 02 09 01 96 ff 03 85 12 b1 02 05 0d 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 85 0c b1 02 c0 c0",
            input_info=(BusType.USB, 0x25AA, 0x8883),
        )


class TestTrs_star_238f_0001(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test trs-star_238f_0001",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 00 09 42 15 00 25 01 75 01 95 01 81 02 09 32 95 01 81 03 09 37 95 01 81 03 95 01 81 03 15 00 25 0f 75 04 09 51 95 01 81 02 09 54 95 01 81 02 09 55 95 01 81 02 05 01 26 ff 03 15 00 75 10 65 00 09 30 95 01 81 02 09 31 81 02 c0 05 0d 09 0e 85 02 09 23 a1 02 15 00 25 0a 09 52 75 08 95 01 b1 02 09 53 95 01 b1 02 09 55 95 01 b1 02 c0 c0",
            input_info=(BusType.USB, 0x238F, 0x0001),
        )


class TestUnitec_227d_0103(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test unitec_227d_0103",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 16 00 00 26 ff 4f 36 00 00 46 6c 03 81 02 09 31 16 00 00 26 ff 3b 36 00 00 46 ed 01 81 02 26 00 00 46 00 00 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 16 00 00 26 ff 4f 36 00 00 46 6c 03 81 02 09 31 16 00 00 26 ff 3b 36 00 00 46 ed 01 81 02 26 00 00 46 00 00 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 16 00 00 26 ff 4f 36 00 00 46 6c 03 81 02 09 31 16 00 00 26 ff 3b 36 00 00 46 ed 01 81 02 26 00 00 46 00 00 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 16 00 00 26 ff 4f 36 00 00 46 6c 03 81 02 09 31 16 00 00 26 ff 3b 36 00 00 46 ed 01 81 02 26 00 00 46 00 00 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 09 51 75 08 95 01 81 02 05 01 35 00 55 0e 65 33 75 10 95 01 09 30 16 00 00 26 ff 4f 36 00 00 46 6c 03 81 02 09 31 16 00 00 26 ff 3b 36 00 00 46 ed 01 81 02 26 00 00 46 00 00 c0 05 0d 09 54 75 08 95 01 81 02 05 0d 85 03 09 55 25 05 75 08 95 01 b1 02 c0 05 0d 09 0e a1 01 85 04 09 53 15 00 25 05 75 08 95 01 b1 02 c0",
            input_info=(BusType.USB, 0x227D, 0x0103),
        )


class TestZytronic_14c8_0005(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test zytronic_14c8_0005",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 95 01 81 02 95 06 81 01 05 01 26 00 10 75 10 95 01 65 00 09 30 81 02 09 31 46 00 10 81 02 05 0d 09 51 26 ff 00 75 08 95 01 81 02 c0 85 02 09 55 15 00 25 08 75 08 95 01 b1 02 c0 05 0d 09 0e a1 01 85 03 a1 02 09 23 09 52 09 53 15 00 25 08 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 04 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 15 00 26 00 10 35 00 46 00 10 65 00 75 10 95 02 81 62 c0 c0 06 00 ff 09 01 a1 01 85 05 09 00 15 00 26 ff 00 75 08 95 3f b1 02 c0 06 00 ff 09 01 a1 01 85 06 09 00 15 00 26 ff 00 75 08 95 3f 81 02 c0",
            input_info=(BusType.USB, 0x14C8, 0x0005),
        )


class TestZytronic_14c8_0006(BaseTest.TestMultitouch):
    def create_device(self):
        return Digitizer(
            "uhid test zytronic_14c8_0006",
            rdesc="05 0d 09 04 a1 01 85 01 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 10 75 10 09 30 81 02 09 31 81 02 05 0d c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 10 75 10 09 30 81 02 09 31 81 02 05 0d c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 10 75 10 09 30 81 02 09 31 81 02 05 0d c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 10 75 10 09 30 81 02 09 31 81 02 05 0d c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 10 75 10 09 30 81 02 09 31 81 02 05 0d c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 10 75 10 09 30 81 02 09 31 81 02 05 0d c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 10 75 10 09 30 81 02 09 31 81 02 05 0d c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 10 75 10 09 30 81 02 09 31 81 02 05 0d c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 10 75 10 09 30 81 02 09 31 81 02 05 0d c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 95 06 81 03 75 08 09 51 95 01 81 02 05 01 26 00 10 75 10 09 30 81 02 09 31 81 02 05 0d c0 05 0d 09 54 95 01 75 08 15 00 25 3c 81 02 05 0d 85 02 09 55 95 01 75 08 15 00 25 3c b1 02 c0 09 0e a1 01 85 03 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 04 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 15 00 26 00 10 35 00 46 00 10 65 00 75 10 95 02 81 62 c0 c0 06 00 ff 09 01 a1 01 85 05 09 00 15 00 26 ff 00 75 08 95 3f b1 02 c0 06 00 ff 09 01 a1 01 85 06 09 00 15 00 26 ff 00 75 08 95 3f 81 02 c0",
            input_info=(BusType.USB, 0x14C8, 0x0006),
        )


################################################################################
#
# Windows 8 compatible devices
#
################################################################################


class TestMinWin8TSParallelTriple(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return MinWin8TSParallel(3)


class TestMinWin8TSParallel(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return MinWin8TSParallel(10)


class TestMinWin8TSHybrid(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return MinWin8TSHybrid()


class TestWin8TSConfidence(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Win8TSConfidence(5)

    @pytest.mark.skip_if_uhdev(
        lambda uhdev: "Confidence" not in uhdev.fields,
        "Device not compatible, missing Confidence usage",
    )
    def test_mt_confidence_bad_release(self):
        """Check for the validity of the confidence bit.
        When a contact is marked as not confident, it should be detected
        as a palm from the kernel POV and released.

        Note: if the kernel exports ABS_MT_TOOL_TYPE, it shouldn't release
        the touch but instead convert it to ABS_MT_TOOL_PALM."""
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()

        t0 = Touch(1, 150, 200)
        r = uhdev.event([t0])
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)

        t0.confidence = False
        t0.tipswitch = False
        r = uhdev.event([t0])
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)

        if evdev.absinfo[libevdev.EV_ABS.ABS_MT_TOOL_TYPE] is not None:
            # the kernel exports MT_TOOL_PALM
            assert libevdev.InputEvent(libevdev.EV_ABS.ABS_MT_TOOL_TYPE, 2) in events

        assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 0) in events
        assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1


    @pytest.mark.skip_if_uhdev(
        lambda uhdev: "Confidence" not in uhdev.fields,
        "Device not compatible, missing Confidence usage",
    )
    def test_mt_confidence_bad_multi_release(self):
        """Check for the sticky finger being properly detected.

        We first inject 3 fingers, then release only the second.
        After 100 ms, we should receive a generated event about the
        2 missing fingers being released.
        """
        uhdev = self.uhdev
        evdev = uhdev.get_evdev()

        # send 3 touches
        t0 = Touch(1, 50, 10)
        t1 = Touch(2, 150, 100)
        t2 = Touch(3, 250, 200)
        r = uhdev.event([t0, t1, t2])
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)

        # release the second
        t1.tipswitch = False
        r = uhdev.event([t1])
        events = uhdev.next_sync_events()
        self.debug_reports(r, uhdev, events)

        # only the second is released
        assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] != -1
        assert evdev.slots[1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1
        assert evdev.slots[2][libevdev.EV_ABS.ABS_MT_TRACKING_ID] != -1

        # wait for the timer to kick in
        time.sleep(0.2)

        events = uhdev.next_sync_events()
        self.debug_reports([], uhdev, events)

        # now all 3 fingers are released
        assert libevdev.InputEvent(libevdev.EV_KEY.BTN_TOUCH, 0) in events
        assert evdev.slots[0][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1
        assert evdev.slots[1][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1
        assert evdev.slots[2][libevdev.EV_ABS.ABS_MT_TRACKING_ID] == -1


class TestElanXPS9360(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test ElanXPS9360",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 a4 26 20 0d 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 50 07 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 a4 26 20 0d 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 50 07 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 a4 26 20 0d 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 50 07 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 a4 26 20 0d 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 50 07 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 a4 26 20 0d 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 50 07 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 a4 26 20 0d 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 50 07 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 a4 26 20 0d 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 50 07 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 a4 26 20 0d 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 50 07 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 a4 26 20 0d 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 50 07 46 a6 00 09 31 81 02 b4 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 a4 26 20 0d 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 50 07 46 a6 00 09 31 81 02 b4 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 85 0a 09 55 25 0a b1 02 85 44 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 ff 01 09 01 a1 01 85 02 15 00 26 ff 00 75 08 95 40 09 00 81 02 c0 06 00 ff 09 01 a1 01 85 03 75 08 95 1f 09 01 91 02 c0 06 01 ff 09 01 a1 01 85 04 15 00 26 ff 00 75 08 95 13 09 00 81 02 c0",
        )


class TestTouchpadXPS9360(BaseTest.TestPTP):
    def create_device(self):
        return PTP(
            "uhid test TouchpadXPS9360",
            max_contacts=5,
            rdesc="05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 01 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0 05 0d 09 05 a1 01 85 03 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 c0 04 75 10 55 0e 65 11 09 30 35 00 46 f5 03 95 01 81 02 46 36 02 26 a8 02 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 c0 04 75 10 55 0e 65 11 09 30 35 00 46 f5 03 95 01 81 02 46 36 02 26 a8 02 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 c0 04 75 10 55 0e 65 11 09 30 35 00 46 f5 03 95 01 81 02 46 36 02 26 a8 02 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 c0 04 75 10 55 0e 65 11 09 30 35 00 46 f5 03 95 01 81 02 46 36 02 26 a8 02 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 c0 04 75 10 55 0e 65 11 09 30 35 00 46 f5 03 95 01 81 02 46 36 02 26 a8 02 09 31 81 02 c0 05 0d 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 25 7f 95 01 75 08 81 02 05 09 09 01 25 01 75 01 95 01 81 02 95 07 81 03 05 0d 85 08 09 55 09 59 75 04 95 02 25 0f b1 02 85 0d 09 60 75 01 95 01 15 00 25 01 b1 02 95 07 b1 03 85 07 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 0e a1 01 85 04 09 22 a1 02 09 52 15 00 25 0a 75 08 95 01 b1 02 c0 09 22 a1 00 85 06 09 57 09 58 75 01 95 02 25 01 b1 02 95 06 b1 03 c0 c0 06 00 ff 09 01 a1 01 85 09 09 02 15 00 26 ff 00 75 08 95 14 91 02 85 0a 09 03 15 00 26 ff 00 75 08 95 14 91 02 85 0b 09 04 15 00 26 ff 00 75 08 95 3d 81 02 85 0c 09 05 15 00 26 ff 00 75 08 95 3d 81 02 85 0f 09 06 15 00 26 ff 00 75 08 95 03 b1 02 85 0e 09 07 15 00 26 ff 00 75 08 95 01 b1 02 c0",
        )


class TestSurfaceBook2(BaseTest.TestPTP):
    def create_device(self):
        return PTP(
            "uhid test SurfaceBook2",
            max_contacts=5,
            rdesc="05 01 09 06 A1 01 85 01 14 25 01 75 01 95 08 05 07 19 E0 29 E7 81 02 75 08 95 0A 18 29 91 26 FF 00 80 05 0C 0A C0 02 A1 02 1A C1 02 2A C6 02 95 06 B1 03 C0 05 08 19 01 29 03 75 01 95 03 25 01 91 02 95 05 91 01 C0 05 01 09 02 A1 01 85 02 05 09 19 01 29 05 81 02 95 01 75 03 81 03 15 81 25 7F 75 08 95 02 05 01 09 30 09 31 81 06 A1 02 09 48 14 25 01 35 01 45 10 75 02 95 01 A4 B1 02 09 38 15 81 25 7F 34 44 75 08 81 06 C0 A1 02 09 48 B4 B1 02 34 44 75 04 B1 03 05 0C 0A 38 02 15 81 25 7F 75 08 81 06 C0 C0 05 0C 09 01 A1 01 85 03 75 10 14 26 FF 03 18 2A FF 03 80 C0 06 05 FF 09 01 A1 01 85 0D 25 FF 95 02 75 08 09 20 81 02 09 22 91 02 15 81 25 7F 95 20 75 08 09 21 81 02 09 23 91 02 C0 09 02 A1 01 85 0C 14 25 FF 95 01 08 91 02 C0 05 0D 09 05 A1 01 85 04 09 22 A1 02 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 03 09 51 81 02 75 01 95 03 81 03 05 01 26 E4 07 75 10 55 0E 65 11 09 30 46 F2 03 95 01 81 02 46 94 02 26 29 05 09 31 81 02 44 54 64 C0 05 0D 09 22 A1 02 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 03 09 51 81 02 75 01 95 03 81 03 05 01 26 E4 07 75 10 55 0E 65 11 09 30 46 F2 03 95 01 81 02 46 94 02 26 29 05 09 31 81 02 44 54 64 C0 05 0D 09 22 A1 02 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 03 09 51 81 02 75 01 95 03 81 03 05 01 26 E4 07 75 10 55 0E 65 11 09 30 46 F2 03 95 01 81 02 46 94 02 26 29 05 09 31 81 02 C0 05 0D 09 22 A1 02 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 03 09 51 81 02 75 01 95 03 81 03 05 01 26 E4 07 75 10 55 0E 65 11 09 30 46 F2 03 95 01 81 02 46 94 02 26 29 05 09 31 81 02 44 54 64 C0 05 0D 09 22 A1 02 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 03 09 51 81 02 75 01 95 03 81 03 05 01 26 E4 07 75 10 55 0E 65 11 09 30 46 F2 03 95 01 81 02 46 94 02 26 29 05 09 31 81 02 C0 05 0D 55 0C 66 01 10 47 FF FF 00 00 27 FF FF 00 00 09 56 81 02 09 54 25 7F 75 08 81 02 05 09 09 01 25 01 75 01 81 02 95 07 81 03 05 0D 85 04 09 55 09 59 75 04 95 02 25 0F B1 02 06 00 FF 09 C6 85 05 14 25 08 75 08 95 01 B1 02 09 C7 26 FF 00 75 08 95 20 B1 02 C0 05 0D 09 0E A1 01 85 07 09 22 A1 02 09 52 14 25 0A 75 08 95 01 B1 02 C0 09 22 A0 85 08 09 57 09 58 75 01 95 02 25 01 B1 02 95 06 B1 03 C0 C0 06 07 FF 09 01 A1 01 85 0A 09 02 26 FF 00 75 08 95 14 91 02 85 09 09 03 91 02 85 0A 09 04 95 26 81 02 85 09 09 05 81 02 85 09 09 06 95 01 B1 02 85 0B 09 07 B1 02 C0 06 05 FF 09 04 A1 01 85 0E 09 31 91 02 09 31 81 03 09 30 91 02 09 30 81 02 95 39 09 32 92 02 01 09 32 82 02 01 C0 06 05 FF 09 50 A1 01 85 20 14 25 FF 75 08 95 3C 09 60 82 02 01 09 61 92 02 01 09 62 B2 02 01 85 21 09 63 82 02 01 09 64 92 02 01 09 65 B2 02 01 85 22 25 FF 75 20 95 04 19 66 29 69 81 02 19 6A 29 6D 91 02 19 6E 29 71 B1 02 85 23 19 72 29 75 81 02 19 76 29 79 91 02 19 7A 29 7D B1 02 85 24 19 7E 29 81 81 02 19 82 29 85 91 02 19 86 29 89 B1 02 85 25 19 8A 29 8D 81 02 19 8E 29 91 91 02 19 92 29 95 B1 02 85 26 19 96 29 99 81 02 19 9A 29 9D 91 02 19 9E 29 A1 B1 02 85 27 19 A2 29 A5 81 02 19 A6 29 A9 91 02 19 AA 29 AD B1 02 85 28 19 AE 29 B1 81 02 19 B2 29 B5 91 02 19 B6 29 B9 B1 02 85 29 19 BA 29 BD 81 02 19 BE 29 C1 91 02 19 C2 29 C5 B1 02 C0 06 00 FF 0A 00 F9 A1 01 85 32 75 10 95 02 14 27 FF FF 00 00 0A 01 F9 B1 02 75 20 95 01 25 FF 0A 02 F9 B1 02 75 08 95 08 26 FF 00 0A 03 F9 B2 02 01 95 10 0A 04 F9 B2 02 01 0A 05 F9 B2 02 01 95 01 75 10 27 FF FF 00 00 0A 06 F9 81 02 C0",
        )


class Test3m_0596_051c(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test 3m_0596_051c",
            rdesc="05 01 09 01 a1 01 85 01 09 01 a1 00 05 09 09 01 95 01 75 01 15 00 25 01 81 02 95 07 75 01 81 03 95 01 75 08 81 03 05 01 09 30 09 31 15 00 26 ff 7f 35 00 46 ff 7f 95 02 75 10 81 02 c0 a1 02 15 00 26 ff 00 09 01 95 39 75 08 81 03 c0 c0 05 0d 09 0e a1 01 85 11 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 09 04 a1 01 85 13 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 81 03 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 d1 12 81 02 09 31 46 b2 0b 81 02 06 00 ff 75 10 95 02 09 01 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 81 03 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 d1 12 81 02 09 31 46 b2 0b 81 02 06 00 ff 75 10 95 02 09 01 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 81 03 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 d1 12 81 02 09 31 46 b2 0b 81 02 06 00 ff 75 10 95 02 09 01 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 81 03 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 d1 12 81 02 09 31 46 b2 0b 81 02 06 00 ff 75 10 95 02 09 01 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 81 03 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 d1 12 81 02 09 31 46 b2 0b 81 02 06 00 ff 75 10 95 02 09 01 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 81 03 09 47 81 02 95 05 81 03 75 08 09 51 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 d1 12 81 02 09 31 46 b2 0b 81 02 06 00 ff 75 10 95 02 09 01 81 02 c0 05 0d 09 54 95 01 75 08 15 00 25 14 81 02 05 0d 55 0c 66 01 10 35 00 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 05 0d 09 55 85 12 15 00 25 14 75 08 95 01 b1 02 85 44 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 06 00 ff 15 00 26 ff 00 85 03 09 01 75 08 95 07 b1 02 85 04 09 01 75 08 95 17 b1 02 85 05 09 01 75 08 95 47 b1 02 85 06 09 01 75 08 95 07 b1 02 85 73 09 01 75 08 95 07 b1 02 85 08 09 01 75 08 95 07 b1 02 85 09 09 01 75 08 95 3f b1 02 85 0f 09 01 75 08 96 07 02 b1 02 c0",
        )


class Testadvanced_silicon_04e8_2084(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test advanced_silicon_04e8_2084",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 c0 14 81 02 46 ae 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 c0 14 81 02 46 ae 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 c0 14 81 02 46 ae 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 c0 14 81 02 46 ae 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 c0 14 81 02 46 ae 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 c0 14 81 02 46 ae 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 c0 14 81 02 46 ae 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 c0 14 81 02 46 ae 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 c0 14 81 02 46 ae 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 c0 14 81 02 46 ae 0b 09 31 81 02 45 00 c0 05 0d 15 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 25 0a 75 08 09 54 81 02 85 44 09 55 b1 02 85 44 06 00 ff 09 c5 26 ff 00 96 00 01 b1 02 85 f0 09 01 95 04 b1 02 85 f2 09 03 b1 02 09 04 b1 02 09 05 b1 02 95 01 09 06 b1 02 09 07 b1 02 85 f1 09 02 95 07 91 02 85 f3 09 08 95 3d b1 02 c0",
        )


class Testadvanced_silicon_2149_2306(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test advanced_silicon_2149_2306",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f6 13 81 02 46 40 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f6 13 81 02 46 40 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f6 13 81 02 46 40 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f6 13 81 02 46 40 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f6 13 81 02 46 40 0b 09 31 81 02 45 00 c0 05 0d 15 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 25 0a 75 08 09 54 81 02 85 44 09 55 b1 02 85 44 06 00 ff 09 c5 26 ff 00 96 00 01 b1 02 85 f0 09 01 95 04 81 02 85 f2 09 03 b1 02 09 04 b1 02 09 05 b1 02 95 01 09 06 b1 02 09 07 b1 02 85 f1 09 02 95 07 91 02 85 f3 09 08 95 3d b1 02 c0",
        )


class Testadvanced_silicon_2149_230a(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test advanced_silicon_2149_230a",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f6 13 81 02 46 40 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f6 13 81 02 46 40 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f6 13 81 02 46 40 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f6 13 81 02 46 40 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f6 13 81 02 46 40 0b 09 31 81 02 45 00 c0 05 0d 15 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 25 0a 75 08 09 54 81 02 85 44 09 55 b1 02 85 44 06 00 ff 09 c5 26 ff 00 96 00 01 b1 02 85 f0 09 01 95 04 81 02 85 f2 09 03 b1 02 09 04 b1 02 09 05 b1 02 95 01 09 06 b1 02 09 07 b1 02 85 f1 09 02 95 07 91 02 85 f3 09 08 95 3d b1 02 c0",
        )


class Testadvanced_silicon_2149_231c(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test advanced_silicon_2149_231c",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 e2 13 81 02 46 32 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 e2 13 81 02 46 32 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 e2 13 81 02 46 32 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 e2 13 81 02 46 32 0b 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 e2 13 81 02 46 32 0b 09 31 81 02 45 00 c0 05 0d 15 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 25 0a 75 08 09 54 81 02 85 44 09 55 b1 02 85 44 06 00 ff 09 c5 26 ff 00 96 00 01 b1 02 85 f0 09 01 95 04 b1 02 85 f2 09 03 b1 02 09 04 b1 02 09 05 b1 02 95 01 09 06 b1 02 09 07 b1 02 85 f1 09 02 95 07 91 02 85 f3 09 08 95 3d b1 02 c0",
        )


class Testadvanced_silicon_2149_2703(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test advanced_silicon_2149_2703",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 66 17 81 02 46 34 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 66 17 81 02 46 34 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 66 17 81 02 46 34 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 66 17 81 02 46 34 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 66 17 81 02 46 34 0d 09 31 81 02 45 00 c0 05 0d 15 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 25 0a 75 08 09 54 81 02 85 44 09 55 b1 02 85 44 06 00 ff 09 c5 26 ff 00 96 00 01 b1 02 85 f0 09 01 95 04 81 02 85 f2 09 03 b1 02 09 04 b1 02 09 05 b1 02 95 01 09 06 b1 02 09 07 b1 02 85 f1 09 02 95 07 91 02 85 f3 09 08 95 3d b1 02 c0",
        )


class Testadvanced_silicon_2149_270b(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test advanced_silicon_2149_270b",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 52 17 81 02 46 20 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 52 17 81 02 46 20 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 52 17 81 02 46 20 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 52 17 81 02 46 20 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 52 17 81 02 46 20 0d 09 31 81 02 45 00 c0 05 0d 15 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 25 0a 75 08 09 54 81 02 85 44 09 55 b1 02 85 44 06 00 ff 09 c5 26 ff 00 96 00 01 b1 02 85 f0 09 01 95 04 b1 02 85 f2 09 03 b1 02 09 04 b1 02 09 05 b1 02 95 01 09 06 b1 02 09 07 b1 02 85 f1 09 02 95 07 91 02 85 f3 09 08 95 3d b1 02 c0",
        )


class Testadvanced_silicon_2575_0204(BaseTest.TestWin8Multitouch):
    """found on the Dell Canvas 27"""

    def create_device(self):
        return Digitizer(
            "uhid test advanced_silicon_2575_0204",
            rdesc="05 0d 09 04 a1 01 85 01 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 51 75 07 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 4f 17 81 02 46 1d 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 51 75 07 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 4f 17 81 02 46 1d 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 51 75 07 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 4f 17 81 02 46 1d 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 51 75 07 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 4f 17 81 02 46 1d 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 51 75 07 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 4f 17 81 02 46 1d 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 51 75 07 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 4f 17 81 02 46 1d 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 51 75 07 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 4f 17 81 02 46 1d 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 51 75 07 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 4f 17 81 02 46 1d 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 51 75 07 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 4f 17 81 02 46 1d 0d 09 31 81 02 45 00 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 25 7f 09 51 75 07 95 01 81 02 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 4f 17 81 02 46 1d 0d 09 31 81 02 45 00 c0 05 0d 15 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 25 0a 75 08 09 54 81 02 85 42 09 55 25 0a b1 02 85 44 06 00 ff 09 c5 26 ff 00 96 00 01 b1 02 c0 05 01 09 0e a1 01 85 05 05 01 09 08 a1 00 09 30 55 0e 65 11 15 00 26 ff 7f 35 00 46 4f 17 75 10 95 01 81 42 09 31 46 1d 0d 81 42 06 00 ff 09 01 75 20 81 03 05 01 09 37 55 00 65 14 16 98 fe 26 68 01 36 98 fe 46 68 01 75 0f 81 06 05 09 09 01 65 00 15 00 25 01 35 00 45 00 75 01 81 02 05 0d 09 42 81 02 09 51 75 07 25 7f 81 02 05 0d 09 48 55 0e 65 11 15 00 26 ff 7f 35 00 46 ff 7f 75 10 81 02 09 49 81 02 09 3f 55 00 65 14 15 00 26 67 01 35 00 46 67 01 81 0a c0 65 00 35 00 45 00 05 0d 15 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 25 05 75 08 09 54 81 02 85 47 09 55 25 05 b1 02 c0 06 00 ff 09 04 a1 01 85 f0 09 01 75 08 95 04 b1 02 85 f2 09 03 b1 02 09 04 b1 02 09 05 b1 02 85 c0 09 01 95 03 b1 02 85 c2 09 01 95 0f b1 02 85 c4 09 01 95 3e b1 02 85 c5 09 01 95 7e b1 02 85 c6 09 01 95 fe b1 02 85 c8 09 01 96 fe 03 b1 02 85 0a 09 01 95 3f b1 02 c0",
        )


class Testadvanced_silicon_2619_5610(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test advanced_silicon_2619_5610",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 a1 00 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f9 15 81 02 46 73 0c 09 31 81 02 45 00 c0 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 a1 00 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f9 15 81 02 46 73 0c 09 31 81 02 45 00 c0 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 a1 00 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f9 15 81 02 46 73 0c 09 31 81 02 45 00 c0 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 a1 00 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f9 15 81 02 46 73 0c 09 31 81 02 45 00 c0 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 a1 00 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f9 15 81 02 46 73 0c 09 31 81 02 45 00 c0 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 a1 00 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f9 15 81 02 46 73 0c 09 31 81 02 45 00 c0 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 a1 00 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f9 15 81 02 46 73 0c 09 31 81 02 45 00 c0 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 a1 00 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f9 15 81 02 46 73 0c 09 31 81 02 45 00 c0 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 a1 00 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f9 15 81 02 46 73 0c 09 31 81 02 45 00 c0 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 02 81 03 09 51 25 1f 75 05 95 01 81 02 a1 00 05 01 26 ff 7f 75 10 55 0e 65 11 09 30 35 00 46 f9 15 81 02 46 73 0c 09 31 81 02 45 00 c0 c0 05 0d 15 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 25 0a 75 08 09 54 81 02 85 44 09 55 b1 02 85 44 06 00 ff 09 c5 26 ff 00 96 00 01 b1 02 85 f0 09 01 95 04 81 02 85 f2 09 03 b1 02 09 04 b1 02 09 05 b1 02 95 01 09 06 b1 02 09 07 b1 02 85 f1 09 02 95 07 91 02 c0",
        )


class Testatmel_03eb_8409(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test atmel_03eb_8409",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 35 00 35 00 46 18 06 26 77 0f 09 31 81 02 35 00 35 00 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 48 81 02 09 49 81 02 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 35 00 35 00 46 18 06 26 77 0f 09 31 81 02 35 00 35 00 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 48 81 02 09 49 81 02 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 35 00 35 00 46 18 06 26 77 0f 09 31 81 02 35 00 35 00 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 48 81 02 09 49 81 02 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 35 00 35 00 46 18 06 26 77 0f 09 31 81 02 35 00 35 00 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 48 81 02 09 49 81 02 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 35 00 35 00 46 18 06 26 77 0f 09 31 81 02 35 00 35 00 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 48 81 02 09 49 81 02 c0 05 0d 27 ff ff 00 00 75 10 95 01 09 56 81 02 15 00 25 1f 75 05 09 54 95 01 81 02 75 03 25 01 95 01 81 03 75 08 85 02 09 55 25 10 b1 02 06 00 ff 85 05 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 00 a1 01 85 03 09 20 a1 00 15 00 25 01 75 01 95 01 09 42 81 02 09 44 81 02 09 45 81 02 81 03 09 32 81 02 95 03 81 03 05 01 55 0e 65 11 35 00 75 10 95 02 46 c8 0a 26 6f 08 09 30 81 02 46 18 06 26 77 0f 09 31 81 02 05 0d 09 30 15 01 26 ff 00 75 08 95 01 81 02 c0 c0",
        )


class Testatmel_03eb_840b(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test atmel_03eb_840b",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 01 46 00 0a 26 ff 0f 09 30 81 02 09 00 81 03 46 a0 05 26 ff 0f 09 31 81 02 09 00 81 03 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 00 81 03 09 00 81 03 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 01 46 00 0a 26 ff 0f 09 30 81 02 09 00 81 03 46 a0 05 26 ff 0f 09 31 81 02 09 00 81 03 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 00 81 03 09 00 81 03 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 01 46 00 0a 26 ff 0f 09 30 81 02 09 00 81 03 46 a0 05 26 ff 0f 09 31 81 02 09 00 81 03 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 00 81 03 09 00 81 03 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 01 46 00 0a 26 ff 0f 09 30 81 02 09 00 81 03 46 a0 05 26 ff 0f 09 31 81 02 09 00 81 03 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 00 81 03 09 00 81 03 c0 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 95 01 81 03 25 1f 75 05 09 51 81 02 05 01 55 0e 65 11 35 00 75 10 95 01 46 00 0a 26 ff 0f 09 30 81 02 09 00 81 03 46 a0 05 26 ff 0f 09 31 81 02 09 00 81 03 05 0d 95 01 75 08 15 00 26 ff 00 46 ff 00 09 00 81 03 09 00 81 03 c0 05 0d 27 ff ff 00 00 75 10 95 01 09 56 81 02 15 00 25 1f 75 05 09 54 95 01 81 02 75 03 25 01 95 01 81 03 75 08 85 02 09 55 25 10 b1 02 06 00 ff 85 05 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 02 a1 01 85 03 09 20 a1 00 15 00 25 01 75 01 95 01 09 42 81 02 09 44 81 02 09 45 81 02 81 03 09 32 81 02 95 03 81 03 05 01 55 0e 65 11 35 00 75 10 95 02 46 00 0a 26 ff 0f 09 30 81 02 46 a0 05 26 ff 0f 09 31 81 02 05 0d 09 30 15 01 26 ff 00 75 08 95 01 81 02 c0 c0",
        )


class Testdell_044e_1220(BaseTest.TestPTP):
    def create_device(self):
        return PTP(
            "uhid test dell_044e_1220",
            type="pressurepad",
            rdesc="05 01 09 02 a1 01 85 01 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 75 01 95 03 81 02 95 05 81 01 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 09 38 95 01 81 06 05 0c 0a 38 02 81 06 c0 c0 05 0d 09 05 a1 01 85 08 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 af 04 75 10 55 0e 65 11 09 30 35 00 46 e8 03 95 01 81 02 26 7b 02 46 12 02 09 31 81 02 c0 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 05 0d 09 56 81 02 09 54 25 05 95 01 75 08 81 02 05 09 19 01 29 03 25 01 75 01 95 03 81 02 95 05 81 03 05 0d 85 09 09 55 75 08 95 01 25 05 b1 02 06 00 ff 85 0a 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 01 ff 09 01 a1 01 85 03 09 01 15 00 26 ff 00 95 1b 81 02 85 04 09 02 95 50 81 02 85 05 09 03 95 07 b1 02 85 06 09 04 81 02 c0 06 02 ff 09 01 a1 01 85 07 09 02 95 86 75 08 b1 02 c0 05 0d 09 0e a1 01 85 0b 09 22 a1 02 09 52 15 00 25 0a 75 08 95 01 b1 02 c0 09 22 a1 00 85 0c 09 57 09 58 75 01 95 02 25 01 b1 02 95 06 b1 03 c0 c0",
        )


class Testdell_06cb_75db(BaseTest.TestPTP):
    def create_device(self):
        return PTP(
            "uhid test dell_06cb_75db",
            max_contacts=3,
            rdesc="05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 01 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0 05 0d 09 05 a1 01 85 03 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 c8 04 75 10 55 0e 65 11 09 30 35 00 46 fb 03 95 01 81 02 46 6c 02 26 e8 02 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 c8 04 75 10 55 0e 65 11 09 30 35 00 46 fb 03 95 01 81 02 46 6c 02 26 e8 02 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 c8 04 75 10 55 0e 65 11 09 30 35 00 46 fb 03 95 01 81 02 46 6c 02 26 e8 02 09 31 81 02 05 0d c0 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 25 7f 95 01 75 08 81 02 05 09 09 01 25 01 75 01 95 01 81 02 95 07 81 03 05 0d 85 08 09 55 09 59 75 04 95 02 25 0f b1 02 85 0d 09 60 75 01 95 01 15 00 25 01 b1 02 95 07 b1 03 85 07 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 0e a1 01 85 04 09 22 a1 02 09 52 15 00 25 0a 75 08 95 01 b1 02 c0 09 22 a1 00 85 06 09 57 09 58 75 01 95 02 25 01 b1 02 95 06 b1 03 c0 c0 06 00 ff 09 01 a1 01 85 09 09 02 15 00 26 ff 00 75 08 95 14 91 02 85 0a 09 03 15 00 26 ff 00 75 08 95 14 91 02 85 0b 09 04 15 00 26 ff 00 75 08 95 1a 81 02 85 0c 09 05 15 00 26 ff 00 75 08 95 1a 81 02 85 0f 09 06 15 00 26 ff 00 75 08 95 01 b1 02 85 0e 09 07 15 00 26 ff 00 75 08 95 01 b1 02 c0",
        )


class Testegalax_capacitive_0eef_790a(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test egalax_capacitive_0eef_790a",
            max_contacts=10,
            rdesc="05 0d 09 04 a1 01 85 06 05 0d 09 54 75 08 15 00 25 0c 95 01 81 02 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 95 01 15 00 25 20 81 02 05 01 26 ff 0f 75 10 55 0e 65 11 09 30 35 00 46 13 0c 81 02 46 cb 06 09 31 81 02 75 08 95 02 81 03 81 03 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 95 01 15 00 25 20 81 02 05 01 26 ff 0f 75 10 55 0e 65 11 09 30 35 00 46 13 0c 81 02 46 cb 06 09 31 81 02 75 08 95 02 81 03 81 03 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 95 01 15 00 25 20 81 02 05 01 26 ff 0f 75 10 55 0e 65 11 09 30 35 00 46 13 0c 81 02 46 cb 06 09 31 81 02 75 08 95 02 81 03 81 03 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 95 01 15 00 25 20 81 02 05 01 26 ff 0f 75 10 55 0e 65 11 09 30 35 00 46 13 0c 81 02 46 cb 06 09 31 81 02 75 08 95 02 81 03 81 03 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 95 01 15 00 25 20 81 02 05 01 26 ff 0f 75 10 55 0e 65 11 09 30 35 00 46 13 0c 81 02 46 cb 06 09 31 81 02 75 08 95 02 81 03 81 03 c0 05 0d 17 00 00 00 00 27 ff ff ff 7f 75 20 95 01 55 00 65 00 09 56 81 02 09 55 09 53 75 08 95 02 26 ff 00 b1 02 06 00 ff 09 c5 85 07 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 01 a1 01 85 01 09 01 a1 02 05 09 19 01 29 02 15 00 25 01 95 02 75 01 81 02 95 01 75 06 81 01 05 01 09 30 09 31 16 00 00 26 ff 0f 36 00 00 46 ff 0f 66 00 00 75 10 95 02 81 02 c0 c0 06 00 ff 09 01 a1 01 09 01 15 00 26 ff 00 85 03 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0 05 0d 09 0e a1 01 85 05 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0",
        )


class Testelan_04f3_000a(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test elan_04f3_000a",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 00 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 00 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 00 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 00 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 00 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 00 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 00 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 00 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 00 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 00 08 46 a6 00 09 31 81 02 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 85 0a 09 55 25 0a b1 02 85 44 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 ff 01 09 01 a1 01 85 02 15 00 26 ff 00 75 08 95 40 09 00 81 02 c0 06 00 ff 09 01 a1 01 85 03 75 08 95 1f 09 01 91 02 c0 06 01 ff 09 01 a1 01 85 04 15 00 26 ff 00 75 08 95 13 09 00 81 02 c0",
        )


class Testelan_04f3_000c(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test elan_04f3_000c",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 40 0e 75 10 55 0f 65 11 09 30 35 00 46 01 01 95 02 81 02 26 00 08 46 91 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 40 0e 75 10 55 0f 65 11 09 30 35 00 46 01 01 95 02 81 02 26 00 08 46 91 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 40 0e 75 10 55 0f 65 11 09 30 35 00 46 01 01 95 02 81 02 26 00 08 46 91 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 40 0e 75 10 55 0f 65 11 09 30 35 00 46 01 01 95 02 81 02 26 00 08 46 91 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 40 0e 75 10 55 0f 65 11 09 30 35 00 46 01 01 95 02 81 02 26 00 08 46 91 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 40 0e 75 10 55 0f 65 11 09 30 35 00 46 01 01 95 02 81 02 26 00 08 46 91 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 40 0e 75 10 55 0f 65 11 09 30 35 00 46 01 01 95 02 81 02 26 00 08 46 91 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 40 0e 75 10 55 0f 65 11 09 30 35 00 46 01 01 95 02 81 02 26 00 08 46 91 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 40 0e 75 10 55 0f 65 11 09 30 35 00 46 01 01 95 02 81 02 26 00 08 46 91 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 40 0e 75 10 55 0f 65 11 09 30 35 00 46 01 01 95 02 81 02 26 00 08 46 91 00 09 31 81 02 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 85 0a 09 55 25 0a b1 02 85 44 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 ff 01 09 01 a1 01 85 02 15 00 26 ff 00 75 08 95 40 09 00 81 02 c0 06 00 ff 09 01 a1 01 85 03 75 08 95 1f 09 01 91 02 c0 06 01 ff 09 01 a1 01 85 04 15 00 26 ff 00 75 08 95 13 09 00 81 02 c0",
        )


class Testelan_04f3_010c(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test elan_04f3_010c",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c2 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c2 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c2 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c2 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c2 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c2 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c2 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c2 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c2 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c2 00 09 31 81 02 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 85 0a 09 55 25 0a b1 02 85 44 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 ff 01 09 01 a1 01 85 02 15 00 26 ff 00 75 08 95 40 09 00 81 02 c0 06 00 ff 09 01 a1 01 85 03 75 08 95 1f 09 01 91 02 c0 06 01 ff 09 01 a1 01 85 04 15 00 26 ff 00 75 08 95 13 09 00 81 02 c0",
        )


class Testelan_04f3_0125(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test elan_04f3_0125",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c1 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c1 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c1 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c1 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c1 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c1 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c1 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c1 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c1 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 f0 0c 75 10 55 0f 65 11 09 30 35 00 46 58 01 95 02 81 02 26 50 07 46 c1 00 09 31 81 02 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 85 0a 09 55 25 0a b1 02 85 44 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 ff 01 09 01 a1 01 85 02 15 00 26 ff 00 75 08 95 40 09 00 81 02 c0 06 00 ff 09 01 a1 01 85 03 75 08 95 1f 09 01 91 02 c0 06 01 ff 09 01 a1 01 85 04 15 00 26 ff 00 75 08 95 13 09 00 81 02 c0",
        )


class Testelan_04f3_016f(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test elan_04f3_016f",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 75 01 81 03 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 85 0a 09 55 25 0a b1 02 85 44 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 ff 01 09 01 a1 01 85 02 15 00 26 ff 00 75 08 95 40 09 00 81 02 c0 06 00 ff 09 01 a1 01 85 03 75 08 95 1f 09 01 91 02 c0 06 01 ff 09 01 a1 01 85 04 15 00 26 ff 00 75 08 95 13 09 00 81 02 c0",
        )


class Testelan_04f3_0732(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test elan_04f3_0732",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0b 75 10 55 0f 65 11 09 30 35 00 46 ff 00 95 02 81 02 26 40 07 46 85 00 09 31 81 02 c0 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0b 75 10 55 0f 65 11 09 30 35 00 46 ff 00 95 02 81 02 26 40 07 46 85 00 09 31 81 02 c0 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0b 75 10 55 0f 65 11 09 30 35 00 46 ff 00 95 02 81 02 26 40 07 46 85 00 09 31 81 02 c0 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0b 75 10 55 0f 65 11 09 30 35 00 46 ff 00 95 02 81 02 26 40 07 46 85 00 09 31 81 02 c0 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0b 75 10 55 0f 65 11 09 30 35 00 46 ff 00 95 02 81 02 26 40 07 46 85 00 09 31 81 02 c0 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0b 75 10 55 0f 65 11 09 30 35 00 46 ff 00 95 02 81 02 26 40 07 46 85 00 09 31 81 02 c0 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0b 75 10 55 0f 65 11 09 30 35 00 46 ff 00 95 02 81 02 26 40 07 46 85 00 09 31 81 02 c0 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0b 75 10 55 0f 65 11 09 30 35 00 46 ff 00 95 02 81 02 26 40 07 46 85 00 09 31 81 02 c0 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0b 75 10 55 0f 65 11 09 30 35 00 46 ff 00 95 02 81 02 26 40 07 46 85 00 09 31 81 02 c0 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0b 75 10 55 0f 65 11 09 30 35 00 46 ff 00 95 02 81 02 26 40 07 46 85 00 09 31 81 02 c0 05 0d 09 56 55 00 65 00 27 ff ff 00 00 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 85 0a 09 55 25 0a b1 02 85 44 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 ff 01 09 01 a1 01 85 02 15 00 25 ff 75 08 95 40 09 00 81 02 c0 06 00 ff 09 01 a1 01 85 03 75 08 95 1f 09 01 91 02 c0",
        )


class Testelan_04f3_200a(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test elan_04f3_200a",
            rdesc="05 0d 09 04 a1 01 85 01 09 22 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 a1 02 05 0d 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 75 06 09 51 25 3f 81 02 26 ff 00 75 08 09 48 81 02 09 49 81 02 95 01 05 01 26 c0 0e 75 10 55 0f 65 11 09 30 35 00 46 26 01 95 02 81 02 26 40 08 46 a6 00 09 31 81 02 c0 05 0d 09 56 55 00 65 00 27 ff ff 00 00 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 85 0a 09 55 25 0a b1 02 85 0e 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0",
        )


class Testelan_04f3_300b(BaseTest.TestPTP):
    def create_device(self):
        return PTP(
            "uhid test elan_04f3_300b",
            max_contacts=3,
            rdesc="05 01 09 02 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 09 38 15 81 25 7f 75 08 95 03 81 06 05 0c 0a 38 02 95 01 81 06 75 08 95 03 81 03 c0 06 00 ff 85 0d 09 c5 15 00 26 ff 00 75 08 95 04 b1 02 85 0c 09 c6 96 76 02 75 08 b1 02 85 0b 09 c7 95 42 75 08 b1 02 09 01 85 5d 95 1f 75 08 81 06 c0 05 0d 09 05 a1 01 85 04 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 02 25 02 09 51 81 02 75 01 95 04 81 03 05 01 15 00 26 a7 0c 75 10 55 0e 65 13 09 30 35 00 46 9d 01 95 01 81 02 46 25 01 26 2b 09 26 2b 09 09 31 81 02 05 0d 15 00 25 64 95 03 c0 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 25 7f 95 01 75 08 81 02 05 09 09 01 25 01 75 01 95 01 81 02 95 07 81 03 05 0d 85 02 09 55 09 59 75 04 95 02 25 0f b1 02 85 07 09 60 75 01 95 01 15 00 25 01 b1 02 95 0f b1 03 06 00 ff 06 00 ff 85 06 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 0e a1 01 85 03 09 22 a1 00 09 52 15 00 25 0a 75 08 95 02 b1 02 c0 09 22 a1 00 85 05 09 57 09 58 15 00 75 01 95 02 25 03 b1 02 95 0e b1 03 c0 c0",
        )


class Testelan_04f3_3045(BaseTest.TestPTP):
    def create_device(self):
        return PTP(
            "uhid test elan_04f3_3045",
            rdesc="05 01 09 02 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 09 38 15 81 25 7f 75 08 95 03 81 06 05 0c 0a 38 02 95 01 81 06 75 08 95 03 81 03 c0 c0 05 0d 09 05 a1 01 85 04 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 75 01 95 02 81 03 95 01 75 04 25 0f 09 51 81 02 05 01 15 00 26 80 0c 75 10 55 0e 65 13 09 30 35 00 46 90 01 95 01 81 02 46 13 01 26 96 08 26 96 08 09 31 81 02 05 0d 15 00 25 64 95 03 c0 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 25 7f 95 01 75 08 81 02 05 09 09 01 25 01 75 01 95 01 81 02 95 07 81 03 09 c5 75 08 95 04 81 03 05 0d 85 02 09 55 09 59 75 04 95 02 25 0f b1 02 85 07 09 60 75 01 95 01 15 00 25 01 b1 02 95 0f b1 03 06 00 ff 06 00 ff 85 06 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 85 0d 09 c5 15 00 26 ff 00 75 08 95 04 b1 02 85 0c 09 c6 96 8a 02 75 08 b1 02 85 0b 09 c7 95 80 75 08 b1 02 c0 05 0d 09 0e a1 01 85 03 09 22 a1 00 09 52 15 00 25 0a 75 08 95 02 b1 02 c0 09 22 a1 00 85 05 09 57 09 58 15 00 75 01 95 02 25 03 b1 02 95 0e b1 03 c0 c0",
        )


class Testelan_04f3_313a(BaseTest.TestPTP):
    def create_device(self):
        return PTP(
            "uhid test elan_04f3_313a",
            type="touchpad",
            input_info=(BusType.I2C, 0x04F3, 0x313A),
            rdesc="05 01 09 02 a1 01 85 01 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 75 01 95 03 81 02 95 05 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 75 08 95 05 81 03 c0 06 00 ff 09 01 85 0e 09 c5 15 00 26 ff 00 75 08 95 04 b1 02 85 0a 09 c6 15 00 26 ff 00 75 08 95 04 b1 02 c0 06 00 ff 09 01 a1 01 85 5c 09 01 95 0b 75 08 81 06 85 0d 09 c5 15 00 26 ff 00 75 08 95 04 b1 02 85 0c 09 c6 96 80 03 75 08 b1 02 85 0b 09 c7 95 82 75 08 b1 02 c0 05 0d 09 05 a1 01 85 04 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 05 09 09 02 09 03 15 00 25 01 75 01 95 02 81 02 05 0d 95 01 75 04 25 0f 09 51 81 02 05 01 15 00 26 d7 0e 75 10 55 0d 65 11 09 30 35 00 46 44 2f 95 01 81 02 46 12 16 26 eb 06 26 eb 06 09 31 81 02 05 0d 15 00 25 64 95 03 c0 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 25 7f 95 01 75 08 81 02 25 01 75 01 95 08 81 03 09 c5 75 08 95 02 81 03 05 0d 85 02 09 55 09 59 75 04 95 02 25 0f b1 02 85 07 09 60 75 01 95 01 15 00 25 01 b1 02 95 0f b1 03 06 00 ff 06 00 ff 85 06 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 0e a1 01 85 03 09 22 a1 00 09 52 15 00 25 0a 75 10 95 01 b1 02 c0 09 22 a1 00 85 05 09 57 09 58 75 01 95 02 25 01 b1 02 95 0e b1 03 c0 c0 05 01 09 02 a1 01 85 2a 09 01 a1 00 05 09 19 01 29 03 15 00 25 01 75 01 95 03 81 02 95 05 81 03 05 01 09 30 09 31 15 81 25 7f 35 81 45 7f 55 00 65 13 75 08 95 02 81 06 75 08 95 05 81 03 c0 c0",
        )


class Testelo_04e7_0080(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test elo_04e7_0080",
            rdesc="05 0d 09 04 a1 01 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 75 10 09 30 46 7c 24 81 02 09 31 46 96 14 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 75 10 09 30 46 7c 24 81 02 09 31 46 96 14 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 75 10 09 30 46 7c 24 81 02 09 31 46 96 14 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 03 81 03 09 32 95 02 81 02 95 02 81 03 09 51 75 08 95 01 81 02 05 01 26 ff 7f 65 11 55 0e 75 10 09 30 46 7c 24 81 02 09 31 46 96 14 81 02 c0 05 0d 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 75 08 95 01 15 00 25 08 81 02 09 55 b1 02 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0",
        )


class Testilitek_222a_0015(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test ilitek_222a_0015",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 c2 16 35 00 46 b3 08 81 42 09 31 26 c2 0c 46 e4 04 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 c2 16 35 00 46 b3 08 81 42 09 31 26 c2 0c 46 e4 04 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 c2 16 35 00 46 b3 08 81 42 09 31 26 c2 0c 46 e4 04 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 c2 16 35 00 46 b3 08 81 42 09 31 26 c2 0c 46 e4 04 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 c2 16 35 00 46 b3 08 81 42 09 31 26 c2 0c 46 e4 04 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 c2 16 35 00 46 b3 08 81 42 09 31 26 c2 0c 46 e4 04 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 c2 16 35 00 46 b3 08 81 42 09 31 26 c2 0c 46 e4 04 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 c2 16 35 00 46 b3 08 81 42 09 31 26 c2 0c 46 e4 04 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 c2 16 35 00 46 b3 08 81 42 09 31 26 c2 0c 46 e4 04 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 c2 16 35 00 46 b3 08 81 42 09 31 26 c2 0c 46 e4 04 81 42 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 85 02 09 55 25 0a b1 02 06 00 ff 09 c5 85 06 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 00 ff 09 01 a1 01 09 01 85 03 15 00 26 ff 00 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0",
        )


class Testilitek_222a_001c(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test ilitek_222a_001c",
            rdesc="05 0d 09 04 a1 01 85 04 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 74 1d 35 00 46 70 0d 81 42 09 31 26 74 10 46 8f 07 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 74 1d 35 00 46 70 0d 81 42 09 31 26 74 10 46 8f 07 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 74 1d 35 00 46 70 0d 81 42 09 31 26 74 10 46 8f 07 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 74 1d 35 00 46 70 0d 81 42 09 31 26 74 10 46 8f 07 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 74 1d 35 00 46 70 0d 81 42 09 31 26 74 10 46 8f 07 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 74 1d 35 00 46 70 0d 81 42 09 31 26 74 10 46 8f 07 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 74 1d 35 00 46 70 0d 81 42 09 31 26 74 10 46 8f 07 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 74 1d 35 00 46 70 0d 81 42 09 31 26 74 10 46 8f 07 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 74 1d 35 00 46 70 0d 81 42 09 31 26 74 10 46 8f 07 81 42 c0 05 0d 09 22 a1 02 05 0d 95 01 75 06 09 51 15 00 25 3f 81 02 09 42 25 01 75 01 95 01 81 02 75 01 95 01 81 03 05 01 75 10 55 0e 65 11 09 30 26 74 1d 35 00 46 70 0d 81 42 09 31 26 74 10 46 8f 07 81 42 c0 05 0d 09 56 55 00 65 00 27 ff ff ff 7f 95 01 75 20 81 02 09 54 25 7f 95 01 75 08 81 02 85 02 09 55 25 0a b1 02 06 00 ff 09 c5 85 06 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 00 ff 09 01 a1 01 09 01 85 03 15 00 26 ff 00 75 08 95 3f 81 02 06 00 ff 09 01 15 00 26 ff 00 75 08 95 3f 91 02 c0",
        )


class Testite_06cb_2968(BaseTest.TestPTP):
    def create_device(self):
        return PTP(
            "uhid test ite_06cb_2968",
            rdesc="05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 01 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0 05 0d 09 05 a1 01 85 03 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 1b 04 75 10 55 0e 65 11 09 30 35 00 46 6c 03 95 01 81 02 46 db 01 26 3b 02 09 31 81 02 05 0d c0 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 25 7f 95 01 75 08 81 02 05 09 09 01 25 01 75 01 95 01 81 02 95 07 81 03 05 0d 85 08 09 55 09 59 75 04 95 02 25 0f b1 02 85 0d 09 60 75 01 95 01 15 00 25 01 b1 02 95 07 b1 03 85 07 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 0e a1 01 85 04 09 22 a1 02 09 52 15 00 25 0a 75 08 95 01 b1 02 c0 09 22 a1 00 85 06 09 57 09 58 75 01 95 02 25 01 b1 02 95 06 b1 03 c0 c0 06 00 ff 09 01 a1 01 85 09 09 02 15 00 26 ff 00 75 08 95 14 91 02 85 0a 09 03 15 00 26 ff 00 75 08 95 14 91 02 85 0b 09 04 15 00 26 ff 00 75 08 95 1a 81 02 85 0c 09 05 15 00 26 ff 00 75 08 95 1a 81 02 85 0f 09 06 15 00 26 ff 00 75 08 95 01 b1 02 85 0e 09 07 15 00 26 ff 00 75 08 95 01 b1 02 c0",
            max_contacts=5,
            input_info=(0x3, 0x06CB, 0x2968),
        )


class Testn_trig_1b96_0c01(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test n_trig_1b96_0c01",
            rdesc="75 08 15 00 26 ff 00 06 0b ff 09 0b a1 01 95 0f 09 29 85 29 b1 02 95 1f 09 2a 85 2a b1 02 95 3e 09 2b 85 2b b1 02 95 fe 09 2c 85 2c b1 02 96 fe 01 09 2d 85 2d b1 02 95 02 09 48 85 48 b1 02 95 0f 09 2e 85 2e 81 02 95 1f 09 2f 85 2f 81 02 95 3e 09 30 85 30 81 02 95 fe 09 31 85 31 81 02 96 fe 01 09 32 85 32 81 02 75 08 96 fe 0f 09 35 85 35 81 02 c0 05 0d 09 02 a1 01 85 01 09 20 35 00 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 b4 05 0d 09 30 26 00 01 81 02 06 00 ff 09 01 81 02 c0 85 0c 06 00 ff 09 0c 75 08 95 06 26 ff 00 b1 02 85 0b 09 0b 95 02 b1 02 85 11 09 11 b1 02 85 15 09 15 95 05 b1 02 85 18 09 18 95 0c b1 02 c0 05 0d 09 04 a1 01 85 03 06 00 ff 09 01 75 10 95 01 15 00 27 ff ff 00 00 81 02 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 09 32 81 02 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 56 75 20 95 01 27 ff ff ff 0f 81 02 85 04 09 55 75 08 95 01 25 0b b1 02 85 0a 06 00 ff 09 03 15 00 b1 02 85 1b 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0",
        )


class Testn_trig_1b96_0c03(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test n_trig_1b96_0c03",
            rdesc="75 08 15 00 26 ff 00 06 0b ff 09 0b a1 01 95 0f 09 29 85 29 b1 02 95 1f 09 2a 85 2a b1 02 95 3e 09 2b 85 2b b1 02 95 fe 09 2c 85 2c b1 02 96 fe 01 09 2d 85 2d b1 02 95 02 09 48 85 48 b1 02 95 0f 09 2e 85 2e 81 02 95 1f 09 2f 85 2f 81 02 95 3e 09 30 85 30 81 02 95 fe 09 31 85 31 81 02 96 fe 01 09 32 85 32 81 02 75 08 96 fe 0f 09 35 85 35 81 02 c0 05 0d 09 02 a1 01 85 01 09 20 35 00 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 b4 05 0d 09 30 26 00 01 81 02 06 00 ff 09 01 81 02 c0 85 0c 06 00 ff 09 0c 75 08 95 06 26 ff 00 b1 02 85 0b 09 0b 95 02 b1 02 85 11 09 11 b1 02 85 15 09 15 95 05 b1 02 85 18 09 18 95 0c b1 02 c0 05 0d 09 04 a1 01 85 03 06 00 ff 09 01 75 10 95 01 15 00 27 ff ff 00 00 81 02 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 15 0a 26 80 25 81 02 09 31 46 b4 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 56 75 20 95 01 27 ff ff ff 0f 81 02 85 04 09 55 75 08 95 01 25 0b b1 02 85 0a 06 00 ff 09 03 15 00 b1 02 85 1b 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0",
        )


class Testn_trig_1b96_0f00(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test n_trig_1b96_0f00",
            rdesc="75 08 15 00 26 ff 00 06 0b ff 09 0b a1 01 95 0f 09 29 85 29 b1 02 95 1f 09 2a 85 2a b1 02 95 3e 09 2b 85 2b b1 02 95 fe 09 2c 85 2c b1 02 96 fe 01 09 2d 85 2d b1 02 95 02 09 48 85 48 b1 02 95 0f 09 2e 85 2e 81 02 95 1f 09 2f 85 2f 81 02 95 3e 09 30 85 30 81 02 95 fe 09 31 85 31 81 02 96 fe 01 09 32 85 32 81 02 75 08 96 fe 0f 09 35 85 35 81 02 c0 05 0d 09 02 a1 01 85 01 09 20 35 00 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 b4 05 0d 09 30 26 00 01 81 02 06 00 ff 09 01 81 02 c0 85 0c 06 00 ff 09 0c 75 08 95 06 26 ff 00 b1 02 85 0b 09 0b 95 02 b1 02 85 11 09 11 b1 02 85 15 09 15 95 05 b1 02 85 18 09 18 95 0c b1 02 c0 05 0d 09 04 a1 01 85 03 06 00 ff 09 01 75 10 95 01 15 00 27 ff ff 00 00 81 02 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 56 75 20 95 01 27 ff ff ff 0f 81 02 85 04 09 55 75 08 95 01 25 0b b1 02 85 0a 06 00 ff 09 03 15 00 b1 02 85 1b 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0",
        )


class Testn_trig_1b96_0f04(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test n_trig_1b96_0f04",
            rdesc="75 08 15 00 26 ff 00 06 0b ff 09 0b a1 01 95 0f 09 29 85 29 b1 02 95 1f 09 2a 85 2a b1 02 95 3e 09 2b 85 2b b1 02 95 fe 09 2c 85 2c b1 02 96 fe 01 09 2d 85 2d b1 02 95 02 09 48 85 48 b1 02 95 0f 09 2e 85 2e 81 02 95 1f 09 2f 85 2f 81 02 95 3e 09 30 85 30 81 02 95 fe 09 31 85 31 81 02 96 fe 01 09 32 85 32 81 02 75 08 96 fe 0f 09 35 85 35 81 02 c0 05 0d 09 02 a1 01 85 01 09 20 35 00 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0e 65 11 46 7f 0b 26 80 25 81 02 09 31 46 78 06 26 20 1c 81 02 b4 05 0d 09 30 26 00 01 81 02 06 00 ff 09 01 81 02 c0 85 0c 06 00 ff 09 0c 75 08 95 06 26 ff 00 b1 02 85 0b 09 0b 95 02 b1 02 85 11 09 11 b1 02 85 15 09 15 95 05 b1 02 85 18 09 18 95 0c b1 02 c0 05 0d 09 04 a1 01 85 03 06 00 ff 09 01 75 10 95 01 15 00 27 ff ff 00 00 81 02 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 7f 0b 26 80 25 81 02 09 31 46 78 06 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 7f 0b 26 80 25 81 02 09 31 46 78 06 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 56 75 20 95 01 27 ff ff ff 0f 81 02 85 04 09 55 75 08 95 01 25 0b b1 02 85 0a 06 00 ff 09 03 15 00 b1 02 85 1b 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0",
        )


class Testn_trig_1b96_1000(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test n_trig_1b96_1000",
            rdesc="75 08 15 00 26 ff 00 06 0b ff 09 0b a1 01 95 0f 09 29 85 29 b1 02 95 1f 09 2a 85 2a b1 02 95 3e 09 2b 85 2b b1 02 95 fe 09 2c 85 2c b1 02 96 fe 01 09 2d 85 2d b1 02 95 02 09 48 85 48 b1 02 95 0f 09 2e 85 2e 81 02 95 1f 09 2f 85 2f 81 02 95 3e 09 30 85 30 81 02 95 fe 09 31 85 31 81 02 96 fe 01 09 32 85 32 81 02 75 08 96 fe 0f 09 35 85 35 81 02 c0 05 0d 09 02 a1 01 85 01 09 20 35 00 a1 00 09 32 09 42 09 44 09 3c 09 45 15 00 25 01 75 01 95 05 81 02 95 03 81 03 05 01 09 30 75 10 95 01 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 b4 05 0d 09 30 26 00 01 81 02 06 00 ff 09 01 81 02 c0 85 0c 06 00 ff 09 0c 75 08 95 06 26 ff 00 b1 02 85 0b 09 0b 95 02 b1 02 85 11 09 11 b1 02 85 15 09 15 95 05 b1 02 85 18 09 18 95 0c b1 02 c0 05 0d 09 04 a1 01 85 03 06 00 ff 09 01 75 10 95 01 15 00 27 ff ff 00 00 81 02 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 01 81 03 09 47 81 02 95 05 81 03 75 10 09 51 27 ff ff 00 00 95 01 81 02 05 01 09 30 75 10 95 02 a4 55 0e 65 11 46 03 0a 26 80 25 81 02 09 31 46 a1 05 26 20 1c 81 02 05 0d 09 48 95 01 26 80 25 81 02 09 49 26 20 1c 81 02 b4 06 00 ff 09 02 75 08 95 04 15 00 26 ff 00 81 02 c0 05 0d 09 54 95 01 75 08 81 02 09 56 75 20 95 01 27 ff ff ff 0f 81 02 85 04 09 55 75 08 95 01 25 0b b1 02 85 0a 06 00 ff 09 03 15 00 b1 02 85 1b 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0",
        )


class Testsharp_04dd_9681(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test sharp_04dd_9681",
            rdesc="06 00 ff 09 01 a1 01 75 08 26 ff 00 15 00 85 06 95 3f 09 01 91 02 85 05 95 3f 09 01 81 02 c0 05 0d 09 04 a1 01 85 81 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 95 01 81 02 05 01 65 11 55 0f 35 00 46 b0 01 26 80 07 75 10 09 30 81 02 46 f3 00 26 38 04 09 31 81 02 05 0d 09 48 09 49 26 ff 00 95 02 75 08 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 95 01 81 02 05 01 65 11 55 0f 35 00 46 b0 01 26 80 07 75 10 09 30 81 02 46 f3 00 26 38 04 09 31 81 02 05 0d 09 48 09 49 26 ff 00 95 02 75 08 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 95 01 81 02 05 01 65 11 55 0f 35 00 46 b0 01 26 80 07 75 10 09 30 81 02 46 f3 00 26 38 04 09 31 81 02 05 0d 09 48 09 49 26 ff 00 95 02 75 08 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 95 01 81 02 05 01 65 11 55 0f 35 00 46 b0 01 26 80 07 75 10 09 30 81 02 46 f3 00 26 38 04 09 31 81 02 05 0d 09 48 09 49 26 ff 00 95 02 75 08 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 95 01 81 02 05 01 65 11 55 0f 35 00 46 b0 01 26 80 07 75 10 09 30 81 02 46 f3 00 26 38 04 09 31 81 02 05 0d 09 48 09 49 26 ff 00 95 02 75 08 81 02 c0 05 0d 09 56 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 81 02 09 54 95 01 75 08 15 00 25 0a 81 02 85 84 09 55 b1 02 85 87 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 09 0e a1 01 85 83 09 23 a1 02 09 52 09 53 15 00 25 0a 75 08 95 02 b1 02 c0 c0 05 01 09 02 a1 01 09 01 a1 00 85 80 05 09 19 01 29 01 15 00 25 01 95 01 75 01 81 02 95 01 75 07 81 01 05 01 65 11 55 0f 09 30 26 80 07 35 00 46 66 00 75 10 95 01 81 02 09 31 26 38 04 35 00 46 4d 00 81 02 c0 c0",
        )


class Testsipodev_0603_0002(BaseTest.TestPTP):
    def create_device(self):
        return PTP(
            "uhid test sipodev_0603_0002",
            type="clickpad",
            rdesc="05 01 09 02 a1 01 85 03 09 01 a1 00 05 09 19 01 29 02 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 15 80 25 7f 75 08 95 02 81 06 c0 c0 05 0d 09 05 a1 01 85 04 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 75 01 95 02 81 03 95 01 75 04 25 05 09 51 81 02 05 01 15 00 26 44 0a 75 0c 55 0e 65 11 09 30 35 00 46 ac 03 95 01 81 02 46 fe 01 26 34 05 75 0c 09 31 81 02 05 0d c0 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 25 0a 95 01 75 04 81 02 75 01 95 03 81 03 05 09 09 01 25 01 75 01 95 01 81 02 05 0d 85 0a 09 55 09 59 75 04 95 02 25 0f b1 02 85 0b 09 60 75 01 95 01 15 00 25 01 b1 02 95 07 b1 03 85 09 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 0e a1 01 85 06 09 22 a1 02 09 52 15 00 25 0a 75 08 95 01 b1 02 c0 09 22 a1 00 85 07 09 57 09 58 75 01 95 02 25 01 b1 02 95 06 b1 03 c0 c0 05 01 09 0c a1 01 85 08 15 00 25 01 09 c6 75 01 95 01 81 06 75 07 81 03 c0 05 01 09 80 a1 01 85 01 15 00 25 01 75 01 0a 81 00 0a 82 00 0a 83 00 95 03 81 06 95 05 81 01 c0 06 0c 00 09 01 a1 01 85 02 25 01 15 00 75 01 0a b5 00 0a b6 00 0a b7 00 0a cd 00 0a e2 00 0a a2 00 0a e9 00 0a ea 00 95 08 81 02 0a 83 01 0a 6f 00 0a 70 00 0a 88 01 0a 8a 01 0a 92 01 0a a8 02 0a 24 02 95 08 81 02 0a 21 02 0a 23 02 0a 96 01 0a 25 02 0a 26 02 0a 27 02 0a 23 02 0a b1 02 95 08 81 02 c0 06 00 ff 09 01 a1 01 85 05 15 00 26 ff 00 19 01 29 02 75 08 95 05 b1 02 c0",
        )


class Testsynaptics_06cb_1d10(BaseTest.TestWin8Multitouch):
    def create_device(self):
        return Digitizer(
            "uhid test synaptics_06cb_1d10",
            rdesc="05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 75 08 95 02 15 81 25 7f 35 81 45 7f 55 0e 65 11 81 06 c0 c0 05 0d 09 04 a1 01 85 01 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 15 01 26 ff 00 95 01 81 42 05 01 15 00 26 3c 0c 75 10 55 0e 65 11 09 30 35 12 46 2a 0c 81 02 09 31 15 00 26 f1 06 35 12 46 df 06 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 15 01 26 ff 00 95 01 81 42 05 01 15 00 26 3c 0c 75 10 55 0e 65 11 09 30 35 12 46 2a 0c 81 02 09 31 15 00 26 f1 06 35 12 46 df 06 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 15 01 26 ff 00 95 01 81 42 05 01 15 00 26 3c 0c 75 10 55 0e 65 11 09 30 35 12 46 2a 0c 81 02 09 31 15 00 26 f1 06 35 12 46 df 06 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 15 01 26 ff 00 95 01 81 42 05 01 15 00 26 3c 0c 75 10 55 0e 65 11 09 30 35 12 46 2a 0c 81 02 09 31 15 00 26 f1 06 35 12 46 df 06 81 02 c0 05 0d 09 22 a1 02 09 42 15 00 25 01 75 01 95 01 81 02 95 07 81 03 75 08 09 51 15 01 26 ff 00 95 01 81 42 05 01 15 00 26 3c 0c 75 10 55 0e 65 11 09 30 35 12 46 2a 0c 81 02 09 31 15 00 26 f1 06 35 12 46 df 06 81 02 c0 05 0d 05 0d 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 95 01 75 08 15 00 25 0f 81 02 85 08 09 55 b1 03 85 07 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 06 00 ff 09 01 a1 01 85 09 09 02 15 00 26 ff 00 75 08 95 3f 91 02 85 0a 09 03 15 00 26 ff 00 75 08 95 05 91 02 85 0b 09 04 15 00 26 ff 00 75 08 95 3d 81 02 85 0c 09 05 15 00 26 ff 00 75 08 95 01 81 02 85 0f 09 06 15 00 26 ff 00 75 08 95 01 b1 02 c0",
        )


class Testsynaptics_06cb_ce08(BaseTest.TestPTP):
    def create_device(self):
        return PTP(
            "uhid test synaptics_06cb_ce08",
            max_contacts=5,
            physical="Vendor Usage 1",
            input_info=(BusType.I2C, 0x06CB, 0xCE08),
            rdesc="05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 01 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0 05 01 09 02 a1 01 85 18 09 01 a1 00 05 09 19 01 29 03 46 00 00 15 00 25 01 75 01 95 03 81 02 95 05 81 01 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0 06 00 ff 09 02 a1 01 85 20 09 01 a1 00 09 03 15 00 26 ff 00 35 00 46 ff 00 75 08 95 05 81 02 c0 c0 05 0d 09 05 a1 01 85 03 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 f8 04 75 10 55 0e 65 11 09 30 35 00 46 24 04 95 01 81 02 46 30 02 26 a0 02 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 f8 04 75 10 55 0e 65 11 09 30 35 00 46 24 04 95 01 81 02 46 30 02 26 a0 02 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 f8 04 75 10 55 0e 65 11 09 30 35 00 46 24 04 95 01 81 02 46 30 02 26 a0 02 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 f8 04 75 10 55 0e 65 11 09 30 35 00 46 24 04 95 01 81 02 46 30 02 26 a0 02 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 f8 04 75 10 55 0e 65 11 09 30 35 00 46 24 04 95 01 81 02 46 30 02 26 a0 02 09 31 81 02 c0 05 0d 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 25 7f 95 01 75 08 81 02 05 09 09 01 25 01 75 01 95 01 81 02 95 07 81 03 05 0d 85 08 09 55 09 59 75 04 95 02 25 0f b1 02 85 0d 09 60 75 01 95 01 15 00 25 01 b1 02 95 07 b1 03 85 07 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 0e a1 01 85 04 09 22 a1 02 09 52 15 00 25 0a 75 08 95 01 b1 02 c0 09 22 a1 00 85 06 09 57 09 58 75 01 95 02 25 01 b1 02 95 06 b1 03 c0 c0 06 00 ff 09 01 a1 01 85 09 09 02 15 00 26 ff 00 75 08 95 14 91 02 85 0a 09 03 15 00 26 ff 00 75 08 95 14 91 02 85 0b 09 04 15 00 26 ff 00 75 08 95 45 81 02 85 0c 09 05 15 00 26 ff 00 75 08 95 45 81 02 85 0f 09 06 15 00 26 ff 00 75 08 95 03 b1 02 85 0e 09 07 15 00 26 ff 00 75 08 95 01 b1 02 c0",
        )

class Testsynaptics_06cb_ce26(TestWin8TSConfidence):
    def create_device(self):
        return PTP(
            "uhid test synaptics_06cb_ce26",
            max_contacts=5,
            input_info=(BusType.I2C, 0x06CB, 0xCE26),
            rdesc="05 01 09 02 a1 01 85 02 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 75 01 95 02 81 02 95 06 81 01 05 01 09 30 09 31 15 81 25 7f 75 08 95 02 81 06 c0 c0 05 0d 09 05 a1 01 85 03 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 45 05 75 10 55 0e 65 11 09 30 35 00 46 64 04 95 01 81 02 46 a2 02 26 29 03 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 45 05 75 10 55 0e 65 11 09 30 35 00 46 64 04 95 01 81 02 46 a2 02 26 29 03 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 45 05 75 10 55 0e 65 11 09 30 35 00 46 64 04 95 01 81 02 46 a2 02 26 29 03 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 45 05 75 10 55 0e 65 11 09 30 35 00 46 64 04 95 01 81 02 46 a2 02 26 29 03 09 31 81 02 c0 05 0d 09 22 a1 02 15 00 25 01 09 47 09 42 95 02 75 01 81 02 95 01 75 03 25 05 09 51 81 02 75 01 95 03 81 03 05 01 15 00 26 45 05 75 10 55 0e 65 11 09 30 35 00 46 64 04 95 01 81 02 46 a2 02 26 29 03 09 31 81 02 c0 05 0d 55 0c 66 01 10 47 ff ff 00 00 27 ff ff 00 00 75 10 95 01 09 56 81 02 09 54 25 7f 95 01 75 08 81 02 05 09 09 01 25 01 75 01 95 01 81 02 95 07 81 03 05 0d 85 08 09 55 09 59 75 04 95 02 25 0f b1 02 85 0d 09 60 75 01 95 01 15 00 25 01 b1 02 95 07 b1 03 85 07 06 00 ff 09 c5 15 00 26 ff 00 75 08 96 00 01 b1 02 c0 05 0d 09 0e a1 01 85 04 09 22 a1 02 09 52 15 00 25 0a 75 08 95 01 b1 02 c0 09 22 a1 00 85 06 09 57 09 58 75 01 95 02 25 01 b1 02 95 06 b1 03 c0 c0 06 00 ff 09 01 a1 01 85 09 09 02 15 00 26 ff 00 75 08 95 14 91 02 85 0a 09 03 15 00 26 ff 00 75 08 95 14 91 02 85 0b 09 04 15 00 26 ff 00 75 08 95 3d 81 02 85 0c 09 05 15 00 26 ff 00 75 08 95 3d 81 02 85 0f 09 06 15 00 26 ff 00 75 08 95 03 b1 02 85 0e 09 07 15 00 26 ff 00 75 08 95 01 b1 02 c0",
        )
