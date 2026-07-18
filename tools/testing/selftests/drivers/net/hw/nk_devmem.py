#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Test devmem TCP with netkit."""

import os
from devmem_lib import setup_test, run_rx, run_tx, run_tx_chunks, run_rx_hds
from lib.py import ksft_run, ksft_exit, ksft_disruptive
from lib.py import NetDrvContEnv


@ksft_disruptive
def check_nk_rx(cfg) -> None:
    """Run the devmem RX test through netkit."""
    run_rx(cfg)


@ksft_disruptive
def check_nk_tx(cfg) -> None:
    """Run the devmem TX test through netkit."""
    run_tx(cfg)


@ksft_disruptive
def check_nk_tx_chunks(cfg) -> None:
    """Run the devmem TX chunking test through netkit."""
    run_tx_chunks(cfg)


def check_nk_rx_hds(cfg) -> None:
    """Run the HDS test through netkit."""
    run_rx_hds(cfg)


def main() -> None:
    """Run the netkit devmem test cases."""
    with NetDrvContEnv(__file__, rxqueues=2, primary_rx_redirect=True) as cfg:
        setup_test(cfg,
                   os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "ncdevmem"))
        ksft_run([check_nk_rx, check_nk_tx, check_nk_tx_chunks,
                  check_nk_rx_hds], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
