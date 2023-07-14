#!/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# -*- coding: utf-8 -*-
#
# Copyright (c) 2017 Benjamin Tissoires <benjamin.tissoires@gmail.com>
# Copyright (c) 2017 Red Hat, Inc.

import libevdev
import os
import pytest
import time

import logging

from hidtools.device.base_device import BaseDevice, EvdevMatch, SysfsFile
from pathlib import Path
from typing import Final

logger = logging.getLogger("hidtools.test.base")

# application to matches
application_matches: Final = {
    # pyright: ignore
    "Accelerometer": EvdevMatch(
        req_properties=[
            libevdev.INPUT_PROP_ACCELEROMETER,
        ]
    ),
    "Game Pad": EvdevMatch(  # in systemd, this is a lot more complex, but that will do
        requires=[
            libevdev.EV_ABS.ABS_X,
            libevdev.EV_ABS.ABS_Y,
            libevdev.EV_ABS.ABS_RX,
            libevdev.EV_ABS.ABS_RY,
            libevdev.EV_KEY.BTN_START,
        ],
        excl_properties=[
            libevdev.INPUT_PROP_ACCELEROMETER,
        ],
    ),
    "Joystick": EvdevMatch(  # in systemd, this is a lot more complex, but that will do
        requires=[
            libevdev.EV_ABS.ABS_RX,
            libevdev.EV_ABS.ABS_RY,
            libevdev.EV_KEY.BTN_START,
        ],
        excl_properties=[
            libevdev.INPUT_PROP_ACCELEROMETER,
        ],
    ),
    "Key": EvdevMatch(
        requires=[
            libevdev.EV_KEY.KEY_A,
        ],
        excl_properties=[
            libevdev.INPUT_PROP_ACCELEROMETER,
            libevdev.INPUT_PROP_DIRECT,
            libevdev.INPUT_PROP_POINTER,
        ],
    ),
    "Mouse": EvdevMatch(
        requires=[
            libevdev.EV_REL.REL_X,
            libevdev.EV_REL.REL_Y,
            libevdev.EV_KEY.BTN_LEFT,
        ],
        excl_properties=[
            libevdev.INPUT_PROP_ACCELEROMETER,
        ],
    ),
    "Pad": EvdevMatch(
        requires=[
            libevdev.EV_KEY.BTN_0,
        ],
        excludes=[
            libevdev.EV_KEY.BTN_TOOL_PEN,
            libevdev.EV_KEY.BTN_TOUCH,
            libevdev.EV_ABS.ABS_DISTANCE,
        ],
        excl_properties=[
            libevdev.INPUT_PROP_ACCELEROMETER,
        ],
    ),
    "Pen": EvdevMatch(
        requires=[
            libevdev.EV_KEY.BTN_STYLUS,
            libevdev.EV_ABS.ABS_X,
            libevdev.EV_ABS.ABS_Y,
        ],
        excl_properties=[
            libevdev.INPUT_PROP_ACCELEROMETER,
        ],
    ),
    "Stylus": EvdevMatch(
        requires=[
            libevdev.EV_KEY.BTN_STYLUS,
            libevdev.EV_ABS.ABS_X,
            libevdev.EV_ABS.ABS_Y,
        ],
        excl_properties=[
            libevdev.INPUT_PROP_ACCELEROMETER,
        ],
    ),
    "Touch Pad": EvdevMatch(
        requires=[
            libevdev.EV_KEY.BTN_LEFT,
            libevdev.EV_ABS.ABS_X,
            libevdev.EV_ABS.ABS_Y,
        ],
        excludes=[libevdev.EV_KEY.BTN_TOOL_PEN, libevdev.EV_KEY.BTN_STYLUS],
        req_properties=[
            libevdev.INPUT_PROP_POINTER,
        ],
        excl_properties=[
            libevdev.INPUT_PROP_ACCELEROMETER,
        ],
    ),
    "Touch Screen": EvdevMatch(
        requires=[
            libevdev.EV_KEY.BTN_TOUCH,
            libevdev.EV_ABS.ABS_X,
            libevdev.EV_ABS.ABS_Y,
        ],
        excludes=[libevdev.EV_KEY.BTN_TOOL_PEN, libevdev.EV_KEY.BTN_STYLUS],
        req_properties=[
            libevdev.INPUT_PROP_DIRECT,
        ],
        excl_properties=[
            libevdev.INPUT_PROP_ACCELEROMETER,
        ],
    ),
}


class UHIDTestDevice(BaseDevice):
    def __init__(self, name, application, rdesc_str=None, rdesc=None, input_info=None):
        super().__init__(name, application, rdesc_str, rdesc, input_info)
        self.application_matches = application_matches
        if name is None:
            name = f"uhid test {self.__class__.__name__}"
        if not name.startswith("uhid test "):
            name = "uhid test " + self.name
        self.name = name


class BaseTestCase:
    class TestUhid(object):
        syn_event = libevdev.InputEvent(libevdev.EV_SYN.SYN_REPORT)  # type: ignore
        key_event = libevdev.InputEvent(libevdev.EV_KEY)  # type: ignore
        abs_event = libevdev.InputEvent(libevdev.EV_ABS)  # type: ignore
        rel_event = libevdev.InputEvent(libevdev.EV_REL)  # type: ignore
        msc_event = libevdev.InputEvent(libevdev.EV_MSC.MSC_SCAN)  # type: ignore

        # List of kernel modules to load before starting the test
        # if any module is not available (not compiled), the test will skip.
        # Each element is a tuple '(kernel driver name, kernel module)',
        # for example ("playstation", "hid-playstation")
        kernel_modules = []

        def assertInputEventsIn(self, expected_events, effective_events):
            effective_events = effective_events.copy()
            for ev in expected_events:
                assert ev in effective_events
                effective_events.remove(ev)
            return effective_events

        def assertInputEvents(self, expected_events, effective_events):
            remaining = self.assertInputEventsIn(expected_events, effective_events)
            assert remaining == []

        @classmethod
        def debug_reports(cls, reports, uhdev=None, events=None):
            data = [" ".join([f"{v:02x}" for v in r]) for r in reports]

            if uhdev is not None:
                human_data = [
                    uhdev.parsed_rdesc.format_report(r, split_lines=True)
                    for r in reports
                ]
                try:
                    human_data = [
                        f'\n\t       {" " * h.index("/")}'.join(h.split("\n"))
                        for h in human_data
                    ]
                except ValueError:
                    # '/' not found: not a numbered report
                    human_data = ["\n\t      ".join(h.split("\n")) for h in human_data]
                data = [f"{d}\n\t ====> {h}" for d, h in zip(data, human_data)]

            reports = data

            if len(reports) == 1:
                print("sending 1 report:")
            else:
                print(f"sending {len(reports)} reports:")
            for report in reports:
                print("\t", report)

            if events is not None:
                print("events received:", events)

        def create_device(self):
            raise Exception("please reimplement me in subclasses")

        def _load_kernel_module(self, kernel_driver, kernel_module):
            sysfs_path = Path("/sys/bus/hid/drivers")
            if kernel_driver is not None:
                sysfs_path /= kernel_driver
            else:
                # special case for when testing all available modules:
                # we don't know beforehand the name of the module from modinfo
                sysfs_path = Path("/sys/module") / kernel_module.replace("-", "_")
            if not sysfs_path.exists():
                import subprocess

                ret = subprocess.run(["/usr/sbin/modprobe", kernel_module])
                if ret.returncode != 0:
                    pytest.skip(
                        f"module {kernel_module} could not be loaded, skipping the test"
                    )

        @pytest.fixture()
        def load_kernel_module(self):
            for kernel_driver, kernel_module in self.kernel_modules:
                self._load_kernel_module(kernel_driver, kernel_module)
            yield

        @pytest.fixture()
        def new_uhdev(self, load_kernel_module):
            return self.create_device()

        def assertName(self, uhdev):
            evdev = uhdev.get_evdev()
            assert uhdev.name in evdev.name

        @pytest.fixture(autouse=True)
        def context(self, new_uhdev, request):
            try:
                with HIDTestUdevRule.instance():
                    with new_uhdev as self.uhdev:
                        skip_cond = request.node.get_closest_marker("skip_if_uhdev")
                        if skip_cond:
                            test, message, *rest = skip_cond.args

                            if test(self.uhdev):
                                pytest.skip(message)

                        self.uhdev.create_kernel_device()
                        now = time.time()
                        while not self.uhdev.is_ready() and time.time() - now < 5:
                            self.uhdev.dispatch(1)
                        if self.uhdev.get_evdev() is None:
                            logger.warning(
                                f"available list of input nodes: (default application is '{self.uhdev.application}')"
                            )
                            logger.warning(self.uhdev.input_nodes)
                        yield
                        self.uhdev = None
            except PermissionError:
                pytest.skip("Insufficient permissions, run me as root")

        @pytest.fixture(autouse=True)
        def check_taint(self):
            # we are abusing SysfsFile here, it's in /proc, but meh
            taint_file = SysfsFile("/proc/sys/kernel/tainted")
            taint = taint_file.int_value

            yield

            assert taint_file.int_value == taint

        def test_creation(self):
            """Make sure the device gets processed by the kernel and creates
            the expected application input node.

            If this fail, there is something wrong in the device report
            descriptors."""
            uhdev = self.uhdev
            assert uhdev is not None
            assert uhdev.get_evdev() is not None
            self.assertName(uhdev)
            assert len(uhdev.next_sync_events()) == 0
            assert uhdev.get_evdev() is not None


class HIDTestUdevRule(object):
    _instance = None
    """
    A context-manager compatible class that sets up our udev rules file and
    deletes it on context exit.

    This class is tailored to our test setup: it only sets up the udev rule
    on the **second** context and it cleans it up again on the last context
    removed. This matches the expected pytest setup: we enter a context for
    the session once, then once for each test (the first of which will
    trigger the udev rule) and once the last test exited and the session
    exited, we clean up after ourselves.
    """

    def __init__(self):
        self.refs = 0
        self.rulesfile = None

    def __enter__(self):
        self.refs += 1
        if self.refs == 2 and self.rulesfile is None:
            self.create_udev_rule()
            self.reload_udev_rules()

    def __exit__(self, exc_type, exc_value, traceback):
        self.refs -= 1
        if self.refs == 0 and self.rulesfile:
            os.remove(self.rulesfile.name)
            self.reload_udev_rules()

    def reload_udev_rules(self):
        import subprocess

        subprocess.run("udevadm control --reload-rules".split())
        subprocess.run("systemd-hwdb update".split())

    def create_udev_rule(self):
        import tempfile

        os.makedirs("/run/udev/rules.d", exist_ok=True)
        with tempfile.NamedTemporaryFile(
            prefix="91-uhid-test-device-REMOVEME-",
            suffix=".rules",
            mode="w+",
            dir="/run/udev/rules.d",
            delete=False,
        ) as f:
            f.write(
                'KERNELS=="*input*", ATTRS{name}=="*uhid test *", ENV{LIBINPUT_IGNORE_DEVICE}="1"\n'
            )
            f.write(
                'KERNELS=="*input*", ATTRS{name}=="*uhid test * System Multi Axis", ENV{ID_INPUT_TOUCHSCREEN}="", ENV{ID_INPUT_SYSTEM_MULTIAXIS}="1"\n'
            )
            self.rulesfile = f

    @classmethod
    def instance(cls):
        if not cls._instance:
            cls._instance = HIDTestUdevRule()
        return cls._instance
