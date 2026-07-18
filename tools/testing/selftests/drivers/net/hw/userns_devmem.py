#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Devmem tests for non-init userns.
"""

import os

from devmem_lib import run_rx, run_rx_hds, run_tx, run_tx_chunks, setup_test
from lib.py import NetDrvContEnv, ksft_disruptive, ksft_exit, ksft_run


@ksft_disruptive
def check_userns_rx(cfg) -> None:
    """Run the devmem RX test through non-init userns netkit."""
    run_rx(cfg)


@ksft_disruptive
def check_userns_tx(cfg) -> None:
    """Run the devmem TX test through non-init userns netkit."""
    run_tx(cfg)


@ksft_disruptive
def check_userns_tx_chunks(cfg) -> None:
    """Run the devmem TX chunking test through non-init userns netkit."""
    run_tx_chunks(cfg)


def check_userns_rx_hds(cfg) -> None:
    """Run the HDS test through non-init userns netkit."""
    run_rx_hds(cfg)


def main() -> None:
    """Run userns devmem RX selftests against the test environment."""
    with NetDrvContEnv(__file__, userns=True, rxqueues=2,
                       primary_rx_redirect=True) as cfg:
        setup_test(cfg,
                   os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "ncdevmem"))
        ksft_run([check_userns_rx, check_userns_tx, check_userns_tx_chunks,
                  check_userns_rx_hds], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
