#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2017 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2017 Red Hat, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# This is for generic devices

from . import base
import logging

logger = logging.getLogger("hidtools.test.hid")


class TestCollectionOverflow(base.BaseTestCase.TestUhid):
    """
    Test class to test re-allocation of the HID collection stack in
    hid-core.c.
    """

    def create_device(self):
        # fmt: off
        report_descriptor = [
            0x05, 0x01,         # .Usage Page (Generic Desktop)
            0x09, 0x02,         # .Usage (Mouse)
            0xa1, 0x01,         # .Collection (Application)
            0x09, 0x02,         # ..Usage (Mouse)
            0xa1, 0x02,         # ..Collection (Logical)
            0x09, 0x01,         # ...Usage (Pointer)
            0xa1, 0x00,         # ...Collection (Physical)
            0x05, 0x09,         # ....Usage Page (Button)
            0x19, 0x01,         # ....Usage Minimum (1)
            0x29, 0x03,         # ....Usage Maximum (3)
            0x15, 0x00,         # ....Logical Minimum (0)
            0x25, 0x01,         # ....Logical Maximum (1)
            0x75, 0x01,         # ....Report Size (1)
            0x95, 0x03,         # ....Report Count (3)
            0x81, 0x02,         # ....Input (Data,Var,Abs)
            0x75, 0x05,         # ....Report Size (5)
            0x95, 0x01,         # ....Report Count (1)
            0x81, 0x03,         # ....Input (Cnst,Var,Abs)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0xa1, 0x02,         # ....Collection (Logical)
            0x09, 0x01,         # .....Usage (Pointer)
            0x05, 0x01,         # .....Usage Page (Generic Desktop)
            0x09, 0x30,         # .....Usage (X)
            0x09, 0x31,         # .....Usage (Y)
            0x15, 0x81,         # .....Logical Minimum (-127)
            0x25, 0x7f,         # .....Logical Maximum (127)
            0x75, 0x08,         # .....Report Size (8)
            0x95, 0x02,         # .....Report Count (2)
            0x81, 0x06,         # .....Input (Data,Var,Rel)
            0xa1, 0x02,         # ...Collection (Logical)
            0x85, 0x12,         # ....Report ID (18)
            0x09, 0x48,         # ....Usage (Resolution Multiplier)
            0x95, 0x01,         # ....Report Count (1)
            0x75, 0x02,         # ....Report Size (2)
            0x15, 0x00,         # ....Logical Minimum (0)
            0x25, 0x01,         # ....Logical Maximum (1)
            0x35, 0x01,         # ....Physical Minimum (1)
            0x45, 0x0c,         # ....Physical Maximum (12)
            0xb1, 0x02,         # ....Feature (Data,Var,Abs)
            0x85, 0x1a,         # ....Report ID (26)
            0x09, 0x38,         # ....Usage (Wheel)
            0x35, 0x00,         # ....Physical Minimum (0)
            0x45, 0x00,         # ....Physical Maximum (0)
            0x95, 0x01,         # ....Report Count (1)
            0x75, 0x10,         # ....Report Size (16)
            0x16, 0x01, 0x80,   # ....Logical Minimum (-32767)
            0x26, 0xff, 0x7f,   # ....Logical Maximum (32767)
            0x81, 0x06,         # ....Input (Data,Var,Rel)
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ...End Collection
            0xc0,               # ..End Collection
            0xc0,               # .End Collection
        ]
        # fmt: on
        return base.UHIDTestDevice(
            name=None, rdesc=report_descriptor, application="Mouse"
        )

    def test_rdesc(self):
        """
        This test can only check for negatives. If the kernel crashes, you
        know why. If this test passes, either the bug isn't present or just
        didn't get triggered. No way to know.

        For an explanation, see kernel patch
            HID: core: replace the collection tree pointers with indices
        """
        pass
