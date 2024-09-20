# SPDX-License-Identifier: GPL-2.0

import sys
from pathlib import Path

KSFT_DIR = (Path(__file__).parent / "../../../../..").resolve()

try:
    sys.path.append(KSFT_DIR.as_posix())
    from net.lib.py import *
    from drivers.net.lib.py import *
except ModuleNotFoundError as e:
    ksft_pr("Failed importing `net` library from kernel sources")
    ksft_pr(str(e))
    ktap_result(True, comment="SKIP")
    sys.exit(4)
