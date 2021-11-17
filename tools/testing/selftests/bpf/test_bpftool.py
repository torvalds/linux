# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2020 SUSE LLC.

import collections
import functools
import json
import os
import socket
import subprocess
import unittest


# Add the source tree of bpftool and /usr/local/sbin to PATH
cur_dir = os.path.dirname(os.path.realpath(__file__))
bpftool_dir = os.path.abspath(os.path.join(cur_dir, "..", "..", "..", "..",
                                           "tools", "bpf", "bpftool"))
os.environ["PATH"] = bpftool_dir + ":/usr/local/sbin:" + os.environ["PATH"]


class IfaceNotFoundError(Exception):
    pass


class UnprivilegedUserError(Exception):
    pass


def _bpftool(args, json=True):
    _args = ["bpftool"]
    if json:
        _args.append("-j")
    _args.extend(args)

    return subprocess.check_output(_args)


def bpftool(args):
    return _bpftool(args, json=False).decode("utf-8")


def bpftool_json(args):
    res = _bpftool(args)
    return json.loads(res)


def get_default_iface():
    for iface in socket.if_nameindex():
        if iface[1] != "lo":
            return iface[1]
    raise IfaceNotFoundError("Could not find any network interface to probe")


def default_iface(f):
    @functools.wraps(f)
    def wrapper(*args, **kwargs):
        iface = get_default_iface()
        return f(*args, iface, **kwargs)
    return wrapper


class TestBpftool(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.getuid() != 0:
            raise UnprivilegedUserError(
                "This test suite needs root privileges")

    @default_iface
    def test_feature_dev_json(self, iface):
        unexpected_helpers = [
            "bpf_probe_write_user",
            "bpf_trace_printk",
        ]
        expected_keys = [
            "syscall_config",
            "program_types",
            "map_types",
            "helpers",
            "misc",
        ]

        res = bpftool_json(["feature", "probe", "dev", iface])
        # Check if the result has all expected keys.
        self.assertCountEqual(res.keys(), expected_keys)
        # Check if unexpected helpers are not included in helpers probes
        # result.
        for helpers in res["helpers"].values():
            for unexpected_helper in unexpected_helpers:
                self.assertNotIn(unexpected_helper, helpers)

    def test_feature_kernel(self):
        test_cases = [
            bpftool_json(["feature", "probe", "kernel"]),
            bpftool_json(["feature", "probe"]),
            bpftool_json(["feature"]),
        ]
        unexpected_helpers = [
            "bpf_probe_write_user",
            "bpf_trace_printk",
        ]
        expected_keys = [
            "syscall_config",
            "system_config",
            "program_types",
            "map_types",
            "helpers",
            "misc",
        ]

        for tc in test_cases:
            # Check if the result has all expected keys.
            self.assertCountEqual(tc.keys(), expected_keys)
            # Check if unexpected helpers are not included in helpers probes
            # result.
            for helpers in tc["helpers"].values():
                for unexpected_helper in unexpected_helpers:
                    self.assertNotIn(unexpected_helper, helpers)

    def test_feature_kernel_full(self):
        test_cases = [
            bpftool_json(["feature", "probe", "kernel", "full"]),
            bpftool_json(["feature", "probe", "full"]),
        ]
        expected_helpers = [
            "bpf_probe_write_user",
            "bpf_trace_printk",
        ]

        for tc in test_cases:
            # Check if expected helpers are included at least once in any
            # helpers list for any program type. Unfortunately we cannot assume
            # that they will be included in all program types or a specific
            # subset of programs. It depends on the kernel version and
            # configuration.
            found_helpers = False

            for helpers in tc["helpers"].values():
                if all(expected_helper in helpers
                       for expected_helper in expected_helpers):
                    found_helpers = True
                    break

            self.assertTrue(found_helpers)

    def test_feature_kernel_full_vs_not_full(self):
        full_res = bpftool_json(["feature", "probe", "full"])
        not_full_res = bpftool_json(["feature", "probe"])
        not_full_set = set()
        full_set = set()

        for helpers in full_res["helpers"].values():
            for helper in helpers:
                full_set.add(helper)

        for helpers in not_full_res["helpers"].values():
            for helper in helpers:
                not_full_set.add(helper)

        self.assertCountEqual(full_set - not_full_set,
                                {"bpf_probe_write_user", "bpf_trace_printk"})
        self.assertCountEqual(not_full_set - full_set, set())

    def test_feature_macros(self):
        expected_patterns = [
            r"/\*\*\* System call availability \*\*\*/",
            r"#define HAVE_BPF_SYSCALL",
            r"/\*\*\* eBPF program types \*\*\*/",
            r"#define HAVE.*PROG_TYPE",
            r"/\*\*\* eBPF map types \*\*\*/",
            r"#define HAVE.*MAP_TYPE",
            r"/\*\*\* eBPF helper functions \*\*\*/",
            r"#define HAVE.*HELPER",
            r"/\*\*\* eBPF misc features \*\*\*/",
        ]

        res = bpftool(["feature", "probe", "macros"])
        for pattern in expected_patterns:
            self.assertRegex(res, pattern)
