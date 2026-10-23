#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
GRO conformance tests against the HW GRO implementation (rx-gro-hw).

See gro_lib.py for the list of test cases.
"""

from gro_lib import gro_main

if __name__ == "__main__":
    gro_main(__file__, "hw")
