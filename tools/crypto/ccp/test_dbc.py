#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0
import unittest
import os
import time
import glob
import fcntl
try:
    import ioctl_opt as ioctl
except ImportError:
    ioctl = None
    pass
from dbc import *

# Artificial delay between set commands
SET_DELAY = 0.5


class invalid_param(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_uint8),
    ]


def system_is_secured() -> bool:
    fused_part = glob.glob("/sys/bus/pci/drivers/ccp/**/fused_part")[0]
    if os.path.exists(fused_part):
        with open(fused_part, "r") as r:
            return int(r.read()) == 1
    return True


class DynamicBoostControlTest(unittest.TestCase):
    def __init__(self, data) -> None:
        self.d = None
        self.signature = b"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
        self.uid = b"1111111111111111"
        super().__init__(data)

    def setUp(self) -> None:
        self.d = open(DEVICE_NODE)
        return super().setUp()

    def tearDown(self) -> None:
        if self.d:
            self.d.close()
        return super().tearDown()


class TestUnsupportedSystem(DynamicBoostControlTest):
    def setUp(self) -> None:
        if os.path.exists(DEVICE_NODE):
            self.skipTest("system is supported")
        with self.assertRaises(FileNotFoundError) as error:
            super().setUp()
        self.assertEqual(error.exception.errno, 2)

    def test_unauthenticated_nonce(self) -> None:
        """fetch unauthenticated nonce"""
        with self.assertRaises(ValueError) as error:
            get_nonce(self.d, None)


class TestInvalidIoctls(DynamicBoostControlTest):
    def __init__(self, data) -> None:
        self.data = invalid_param()
        self.data.data = 1
        super().__init__(data)

    def setUp(self) -> None:
        if not os.path.exists(DEVICE_NODE):
            self.skipTest("system is unsupported")
        if not ioctl:
            self.skipTest("unable to test IOCTLs without ioctl_opt")

        return super().setUp()

    def test_invalid_nonce_ioctl(self) -> None:
        """tries to call get_nonce ioctl with invalid data structures"""

        # 0x1 (get nonce), and invalid data
        INVALID1 = ioctl.IOWR(ord("D"), 0x01, invalid_param)
        with self.assertRaises(OSError) as error:
            fcntl.ioctl(self.d, INVALID1, self.data, True)
        self.assertEqual(error.exception.errno, 22)

    def test_invalid_setuid_ioctl(self) -> None:
        """tries to call set_uid ioctl with invalid data structures"""

        # 0x2 (set uid), and invalid data
        INVALID2 = ioctl.IOW(ord("D"), 0x02, invalid_param)
        with self.assertRaises(OSError) as error:
            fcntl.ioctl(self.d, INVALID2, self.data, True)
        self.assertEqual(error.exception.errno, 22)

    def test_invalid_setuid_rw_ioctl(self) -> None:
        """tries to call set_uid ioctl with invalid data structures"""

        # 0x2 as RW (set uid), and invalid data
        INVALID3 = ioctl.IOWR(ord("D"), 0x02, invalid_param)
        with self.assertRaises(OSError) as error:
            fcntl.ioctl(self.d, INVALID3, self.data, True)
        self.assertEqual(error.exception.errno, 22)

    def test_invalid_param_ioctl(self) -> None:
        """tries to call param ioctl with invalid data structures"""
        # 0x3 (param), and invalid data
        INVALID4 = ioctl.IOWR(ord("D"), 0x03, invalid_param)
        with self.assertRaises(OSError) as error:
            fcntl.ioctl(self.d, INVALID4, self.data, True)
        self.assertEqual(error.exception.errno, 22)

    def test_invalid_call_ioctl(self) -> None:
        """tries to call the DBC ioctl with invalid data structures"""
        # 0x4, and invalid data
        INVALID5 = ioctl.IOWR(ord("D"), 0x04, invalid_param)
        with self.assertRaises(OSError) as error:
            fcntl.ioctl(self.d, INVALID5, self.data, True)
        self.assertEqual(error.exception.errno, 22)


class TestInvalidSignature(DynamicBoostControlTest):
    def setUp(self) -> None:
        if not os.path.exists(DEVICE_NODE):
            self.skipTest("system is unsupported")
        if not system_is_secured():
            self.skipTest("system is unfused")
        return super().setUp()

    def test_unauthenticated_nonce(self) -> None:
        """fetch unauthenticated nonce"""
        get_nonce(self.d, None)

    def test_multiple_unauthenticated_nonce(self) -> None:
        """ensure state machine always returns nonce"""
        for count in range(0, 2):
            get_nonce(self.d, None)

    def test_authenticated_nonce(self) -> None:
        """fetch authenticated nonce"""
        with self.assertRaises(OSError) as error:
            get_nonce(self.d, self.signature)
        self.assertEqual(error.exception.errno, 1)

    def test_set_uid(self) -> None:
        """set uid"""
        with self.assertRaises(OSError) as error:
            set_uid(self.d, self.uid, self.signature)
        self.assertEqual(error.exception.errno, 1)

    def test_get_param(self) -> None:
        """fetch a parameter"""
        with self.assertRaises(OSError) as error:
            process_param(self.d, PARAM_GET_SOC_PWR_CUR, self.signature)
        self.assertEqual(error.exception.errno, 1)

    def test_set_param(self) -> None:
        """set a parameter"""
        with self.assertRaises(OSError) as error:
            process_param(self.d, PARAM_SET_PWR_CAP, self.signature, 1000)
        self.assertEqual(error.exception.errno, 1)


class TestUnFusedSystem(DynamicBoostControlTest):
    def setup_identity(self) -> None:
        """sets up the identity of the caller"""
        # if already authenticated these may fail
        try:
            get_nonce(self.d, None)
        except PermissionError:
            pass
        try:
            set_uid(self.d, self.uid, self.signature)
        except BlockingIOError:
            pass
        try:
            get_nonce(self.d, self.signature)
        except PermissionError:
            pass

    def setUp(self) -> None:
        if not os.path.exists(DEVICE_NODE):
            self.skipTest("system is unsupported")
        if system_is_secured():
            self.skipTest("system is fused")
        super().setUp()
        self.setup_identity()
        time.sleep(SET_DELAY)

    def test_get_valid_param(self) -> None:
        """fetch all possible parameters"""
        # SOC power
        soc_power_max = process_param(self.d, PARAM_GET_SOC_PWR_MAX, self.signature)
        soc_power_min = process_param(self.d, PARAM_GET_SOC_PWR_MIN, self.signature)
        self.assertGreater(soc_power_max[0], soc_power_min[0])

        # fmax
        fmax_max = process_param(self.d, PARAM_GET_FMAX_MAX, self.signature)
        fmax_min = process_param(self.d, PARAM_GET_FMAX_MIN, self.signature)
        self.assertGreater(fmax_max[0], fmax_min[0])

        # cap values
        keys = {
            "fmax-cap": PARAM_GET_FMAX_CAP,
            "power-cap": PARAM_GET_PWR_CAP,
            "current-temp": PARAM_GET_CURR_TEMP,
            "soc-power-cur": PARAM_GET_SOC_PWR_CUR,
        }
        for k in keys:
            result = process_param(self.d, keys[k], self.signature)
            self.assertGreater(result[0], 0)

    def test_get_invalid_param(self) -> None:
        """fetch an invalid parameter"""
        try:
            set_uid(self.d, self.uid, self.signature)
        except OSError:
            pass
        with self.assertRaises(OSError) as error:
            process_param(self.d, (0xF,), self.signature)
        self.assertEqual(error.exception.errno, 22)

    def test_set_fmax(self) -> None:
        """get/set fmax limit"""
        # fetch current
        original = process_param(self.d, PARAM_GET_FMAX_CAP, self.signature)

        # set the fmax
        target = original[0] - 100
        process_param(self.d, PARAM_SET_FMAX_CAP, self.signature, target)
        time.sleep(SET_DELAY)
        new = process_param(self.d, PARAM_GET_FMAX_CAP, self.signature)
        self.assertEqual(new[0], target)

        # revert back to current
        process_param(self.d, PARAM_SET_FMAX_CAP, self.signature, original[0])
        time.sleep(SET_DELAY)
        cur = process_param(self.d, PARAM_GET_FMAX_CAP, self.signature)
        self.assertEqual(cur[0], original[0])

    def test_set_power_cap(self) -> None:
        """get/set power cap limit"""
        # fetch current
        original = process_param(self.d, PARAM_GET_PWR_CAP, self.signature)

        # set the fmax
        target = original[0] - 10
        process_param(self.d, PARAM_SET_PWR_CAP, self.signature, target)
        time.sleep(SET_DELAY)
        new = process_param(self.d, PARAM_GET_PWR_CAP, self.signature)
        self.assertEqual(new[0], target)

        # revert back to current
        process_param(self.d, PARAM_SET_PWR_CAP, self.signature, original[0])
        time.sleep(SET_DELAY)
        cur = process_param(self.d, PARAM_GET_PWR_CAP, self.signature)
        self.assertEqual(cur[0], original[0])

    def test_set_3d_graphics_mode(self) -> None:
        """set/get 3d graphics mode"""
        # these aren't currently implemented but may be some day
        # they are *expected* to fail
        with self.assertRaises(OSError) as error:
            process_param(self.d, PARAM_GET_GFX_MODE, self.signature)
        self.assertEqual(error.exception.errno, 2)

        time.sleep(SET_DELAY)

        with self.assertRaises(OSError) as error:
            process_param(self.d, PARAM_SET_GFX_MODE, self.signature, 1)
        self.assertEqual(error.exception.errno, 2)


if __name__ == "__main__":
    unittest.main()
