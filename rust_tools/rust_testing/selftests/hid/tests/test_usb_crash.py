#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2021 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2021 Red Hat, Inc.
#

# This is to ensure we don't crash when emulating USB devices

from . import base
import pytest
import logging

logger = logging.getLogger("hidtools.test.usb")


class USBDev(base.UHIDTestDevice):
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

    def __init__(self, name=None, input_info=None):
        super().__init__(
            name, "Mouse", input_info=input_info, rdesc=USBDev.report_descriptor
        )

    # skip witing for udev events, it's likely that the report
    # descriptor is wrong
    def is_ready(self):
        return True

    # we don't have an evdev node here, so paper over
    # the checks
    def get_evdev(self, application=None):
        return "OK"


class TestUSBDevice(base.BaseTestCase.TestUhid):
    """
    Test class to test if an emulated USB device crashes
    the kernel.
    """

    # conftest.py is generating the following fixture:
    #
    # @pytest.fixture(params=[('modulename', 1, 2)])
    # def usbVidPid(self, request):
    #     return request.param

    @pytest.fixture()
    def new_uhdev(self, usbVidPid, request):
        self.module, self.vid, self.pid = usbVidPid
        self._load_kernel_module(None, self.module)
        return USBDev(input_info=(3, self.vid, self.pid))

    def test_creation(self):
        """
        inject the USB dev through uhid and immediately see if there is a crash:

        uhid can create a USB device with the BUS_USB bus, and some
        drivers assume that they can then access USB related structures
        when they are actually provided a uhid device. This leads to
        a crash because those access result in a segmentation fault.

        The kernel should not crash on any (random) user space correct
        use of its API. So run through all available modules and declared
        devices to see if we can generate a uhid device without a crash.

        The test is empty as the fixture `check_taint` is doing the job (and
        honestly, when the kernel crashes, the whole machine freezes).
        """
        assert True
