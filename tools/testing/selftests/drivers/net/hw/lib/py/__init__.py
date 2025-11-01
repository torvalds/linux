# SPDX-License-Identifier: GPL-2.0

"""
Driver test environment (hardware-only tests).
NetDrvEnv and NetDrvEpEnv are the main environment classes.
Former is for local host only tests, latter creates / connects
to a remote endpoint. See NIPA wiki for more information about
running and writing driver tests.
"""

import sys
from pathlib import Path

KSFT_DIR = (Path(__file__).parent / "../../../../..").resolve()

try:
    sys.path.append(KSFT_DIR.as_posix())

    # Import one by one to avoid pylint false positives
    from net.lib.py import NetNS, NetNSEnter, NetdevSimDev
    from net.lib.py import EthtoolFamily, NetdevFamily, NetshaperFamily, \
        NlError, RtnlFamily, DevlinkFamily, PSPFamily
    from net.lib.py import CmdExitFailure
    from net.lib.py import bkg, cmd, bpftool, bpftrace, defer, ethtool, \
        fd_read_timeout, ip, rand_port, wait_port_listen, wait_file
    from net.lib.py import KsftSkipEx, KsftFailEx, KsftXfailEx
    from net.lib.py import ksft_disruptive, ksft_exit, ksft_pr, ksft_run, \
        ksft_setup
    from net.lib.py import ksft_eq, ksft_ge, ksft_in, ksft_is, ksft_lt, \
        ksft_ne, ksft_not_in, ksft_raises, ksft_true, ksft_gt, ksft_not_none
    from drivers.net.lib.py import GenerateTraffic, Remote
    from drivers.net.lib.py import NetDrvEnv, NetDrvEpEnv

    __all__ = ["NetNS", "NetNSEnter", "NetdevSimDev",
               "EthtoolFamily", "NetdevFamily", "NetshaperFamily",
               "NlError", "RtnlFamily", "DevlinkFamily", "PSPFamily",
               "CmdExitFailure",
               "bkg", "cmd", "bpftool", "bpftrace", "defer", "ethtool",
               "fd_read_timeout", "ip", "rand_port",
               "wait_port_listen", "wait_file",
               "KsftSkipEx", "KsftFailEx", "KsftXfailEx",
               "ksft_disruptive", "ksft_exit", "ksft_pr", "ksft_run",
               "ksft_setup",
               "ksft_eq", "ksft_ge", "ksft_in", "ksft_is", "ksft_lt",
               "ksft_ne", "ksft_not_in", "ksft_raises", "ksft_true", "ksft_gt",
               "ksft_not_none", "ksft_not_none",
               "NetDrvEnv", "NetDrvEpEnv", "GenerateTraffic", "Remote"]
except ModuleNotFoundError as e:
    print("Failed importing `net` library from kernel sources")
    print(str(e))
    sys.exit(4)
