# SPDX-License-Identifier: GPL-2.0

import sys
from pathlib import Path

KSFT_DIR = (Path(__file__).parent / "../../../..").resolve()

try:
    sys.path.append(KSFT_DIR.as_posix())

    from net.lib.py import *

    # Import one by one to avoid pylint false positives
    from net.lib.py import EthtoolFamily, NetdevFamily, NetshaperFamily, \
        NlError, RtnlFamily, DevlinkFamily
    from net.lib.py import CmdExitFailure
    from net.lib.py import bkg, cmd, bpftool, bpftrace, defer, ethtool, \
        fd_read_timeout, ip, rand_port, tool, wait_port_listen
    from net.lib.py import fd_read_timeout
    from net.lib.py import KsftSkipEx, KsftFailEx, KsftXfailEx
    from net.lib.py import ksft_disruptive, ksft_exit, ksft_pr, ksft_run, \
        ksft_setup
    from net.lib.py import ksft_eq, ksft_ge, ksft_in, ksft_is, ksft_lt, \
        ksft_ne, ksft_not_in, ksft_raises, ksft_true
except ModuleNotFoundError as e:
    ksft_pr("Failed importing `net` library from kernel sources")
    ksft_pr(str(e))
    ktap_result(True, comment="SKIP")
    sys.exit(4)

from .env import *
from .load import *
from .remote import Remote
