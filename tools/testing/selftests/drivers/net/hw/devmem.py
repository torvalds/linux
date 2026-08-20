#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from os import path
from devmem_lib import (setup_test, run_rx, run_tx, run_tx_chunks, run_rx_hds,
                        run_rx_large_niov)
from lib.py import ksft_run, ksft_exit, ksft_disruptive
from lib.py import NetDrvEpEnv


@ksft_disruptive
def check_rx(cfg) -> None:
    """Run the devmem RX test."""
    run_rx(cfg)


@ksft_disruptive
def check_tx(cfg) -> None:
    """Run the devmem TX test."""
    run_tx(cfg)


@ksft_disruptive
def check_tx_chunks(cfg) -> None:
    """Run the devmem TX chunking test."""
    run_tx_chunks(cfg)


def check_rx_hds(cfg) -> None:
    """Run the HDS test."""
    run_rx_hds(cfg)


def check_rx_large_niov(cfg) -> None:
    """Run the devmem RX test with rx-page-size = 16 KiB."""
    run_rx_large_niov(cfg)


def main() -> None:
    """Run the devmem test cases."""
    with NetDrvEpEnv(__file__) as cfg:
        setup_test(cfg, path.abspath(path.dirname(__file__) + "/ncdevmem"))
        ksft_run([check_rx, check_tx, check_tx_chunks, check_rx_hds,
                  check_rx_large_niov],
                 args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
