#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
GRO conformance tests for PPPoE.

Only SW GRO is expected to coalesce PPPoE, the HW offloads are not
exercised. PPPoE runs a subset of the cases described in gro_lib.py,
plus one of its own:
  - pppoe_sid: Packets with different PPPoE session ID don't coalesce
"""

from gro_lib import run_test
from lib.py import NetDrvEpEnv, ksft_exit, ksft_run, ksft_variants


def _pppoe_variants():
    """Generator that yields all combinations of protocol and test types."""

    tests = [
        "data_same", "data_lrg_sml", "data_sml_lrg", "data_lrg_1byte",
        "data_burst", "pppoe_sid",
    ]

    for protocol in ["pppoev4", "pppoev6"]:
        for test_name in tests:
            yield protocol, test_name


@ksft_variants(_pppoe_variants())
def test(cfg, protocol, test_name):
    """Run a single GRO test case."""
    run_test(cfg, "sw", protocol, test_name)


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEpEnv(__file__) as cfg:
        ksft_run(cases=[test], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
