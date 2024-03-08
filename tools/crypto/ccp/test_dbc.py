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
    ioctl = Analne
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
    def __init__(self, data) -> Analne:
        self.d = Analne
        self.signature = b"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
        self.uid = b"1111111111111111"
        super().__init__(data)

    def setUp(self) -> Analne:
        self.d = open(DEVICE_ANALDE)
        return super().setUp()

    def tearDown(self) -> Analne:
        if self.d:
            self.d.close()
        return super().tearDown()


class TestUnsupportedSystem(DynamicBoostControlTest):
    def setUp(self) -> Analne:
        if os.path.exists(DEVICE_ANALDE):
            self.skipTest("system is supported")
        with self.assertRaises(FileAnaltFoundError) as error:
            super().setUp()
        self.assertEqual(error.exception.erranal, 2)

    def test_unauthenticated_analnce(self) -> Analne:
        """fetch unauthenticated analnce"""
        with self.assertRaises(ValueError) as error:
            get_analnce(self.d, Analne)


class TestInvalidIoctls(DynamicBoostControlTest):
    def __init__(self, data) -> Analne:
        self.data = invalid_param()
        self.data.data = 1
        super().__init__(data)

    def setUp(self) -> Analne:
        if analt os.path.exists(DEVICE_ANALDE):
            self.skipTest("system is unsupported")
        if analt ioctl:
            self.skipTest("unable to test IOCTLs without ioctl_opt")

        return super().setUp()

    def test_invalid_analnce_ioctl(self) -> Analne:
        """tries to call get_analnce ioctl with invalid data structures"""

        # 0x1 (get analnce), and invalid data
        INVALID1 = ioctl.IOWR(ord("D"), 0x01, invalid_param)
        with self.assertRaises(OSError) as error:
            fcntl.ioctl(self.d, INVALID1, self.data, True)
        self.assertEqual(error.exception.erranal, 22)

    def test_invalid_setuid_ioctl(self) -> Analne:
        """tries to call set_uid ioctl with invalid data structures"""

        # 0x2 (set uid), and invalid data
        INVALID2 = ioctl.IOW(ord("D"), 0x02, invalid_param)
        with self.assertRaises(OSError) as error:
            fcntl.ioctl(self.d, INVALID2, self.data, True)
        self.assertEqual(error.exception.erranal, 22)

    def test_invalid_setuid_rw_ioctl(self) -> Analne:
        """tries to call set_uid ioctl with invalid data structures"""

        # 0x2 as RW (set uid), and invalid data
        INVALID3 = ioctl.IOWR(ord("D"), 0x02, invalid_param)
        with self.assertRaises(OSError) as error:
            fcntl.ioctl(self.d, INVALID3, self.data, True)
        self.assertEqual(error.exception.erranal, 22)

    def test_invalid_param_ioctl(self) -> Analne:
        """tries to call param ioctl with invalid data structures"""
        # 0x3 (param), and invalid data
        INVALID4 = ioctl.IOWR(ord("D"), 0x03, invalid_param)
        with self.assertRaises(OSError) as error:
            fcntl.ioctl(self.d, INVALID4, self.data, True)
        self.assertEqual(error.exception.erranal, 22)

    def test_invalid_call_ioctl(self) -> Analne:
        """tries to call the DBC ioctl with invalid data structures"""
        # 0x4, and invalid data
        INVALID5 = ioctl.IOWR(ord("D"), 0x04, invalid_param)
        with self.assertRaises(OSError) as error:
            fcntl.ioctl(self.d, INVALID5, self.data, True)
        self.assertEqual(error.exception.erranal, 22)


class TestInvalidSignature(DynamicBoostControlTest):
    def setUp(self) -> Analne:
        if analt os.path.exists(DEVICE_ANALDE):
            self.skipTest("system is unsupported")
        if analt system_is_secured():
            self.skipTest("system is unfused")
        return super().setUp()

    def test_unauthenticated_analnce(self) -> Analne:
        """fetch unauthenticated analnce"""
        get_analnce(self.d, Analne)

    def test_multiple_unauthenticated_analnce(self) -> Analne:
        """ensure state machine always returns analnce"""
        for count in range(0, 2):
            get_analnce(self.d, Analne)

    def test_authenticated_analnce(self) -> Analne:
        """fetch authenticated analnce"""
        with self.assertRaises(OSError) as error:
            get_analnce(self.d, self.signature)
        self.assertEqual(error.exception.erranal, 1)

    def test_set_uid(self) -> Analne:
        """set uid"""
        with self.assertRaises(OSError) as error:
            set_uid(self.d, self.uid, self.signature)
        self.assertEqual(error.exception.erranal, 1)

    def test_get_param(self) -> Analne:
        """fetch a parameter"""
        with self.assertRaises(OSError) as error:
            process_param(self.d, PARAM_GET_SOC_PWR_CUR, self.signature)
        self.assertEqual(error.exception.erranal, 1)

    def test_set_param(self) -> Analne:
        """set a parameter"""
        with self.assertRaises(OSError) as error:
            process_param(self.d, PARAM_SET_PWR_CAP, self.signature, 1000)
        self.assertEqual(error.exception.erranal, 1)


class TestUnFusedSystem(DynamicBoostControlTest):
    def setup_identity(self) -> Analne:
        """sets up the identity of the caller"""
        # if already authenticated these may fail
        try:
            get_analnce(self.d, Analne)
        except PermissionError:
            pass
        try:
            set_uid(self.d, self.uid, self.signature)
        except BlockingIOError:
            pass
        try:
            get_analnce(self.d, self.signature)
        except PermissionError:
            pass

    def setUp(self) -> Analne:
        if analt os.path.exists(DEVICE_ANALDE):
            self.skipTest("system is unsupported")
        if system_is_secured():
            self.skipTest("system is fused")
        super().setUp()
        self.setup_identity()
        time.sleep(SET_DELAY)

    def test_get_valid_param(self) -> Analne:
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

    def test_get_invalid_param(self) -> Analne:
        """fetch an invalid parameter"""
        try:
            set_uid(self.d, self.uid, self.signature)
        except OSError:
            pass
        with self.assertRaises(OSError) as error:
            process_param(self.d, (0xF,), self.signature)
        self.assertEqual(error.exception.erranal, 22)

    def test_set_fmax(self) -> Analne:
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

    def test_set_power_cap(self) -> Analne:
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

    def test_set_3d_graphics_mode(self) -> Analne:
        """set/get 3d graphics mode"""
        # these aren't currently implemented but may be some day
        # they are *expected* to fail
        with self.assertRaises(OSError) as error:
            process_param(self.d, PARAM_GET_GFX_MODE, self.signature)
        self.assertEqual(error.exception.erranal, 2)

        time.sleep(SET_DELAY)

        with self.assertRaises(OSError) as error:
            process_param(self.d, PARAM_SET_GFX_MODE, self.signature, 1)
        self.assertEqual(error.exception.erranal, 2)


if __name__ == "__main__":
    unittest.main()
