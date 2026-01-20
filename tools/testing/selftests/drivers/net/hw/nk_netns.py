#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_run, ksft_exit
from lib.py import NetDrvContEnv
from lib.py import cmd


def test_ping(cfg) -> None:
    cfg.require_ipver("6")

    cmd(f"ping -c 1 -W5 {cfg.nk_guest_ipv6}", host=cfg.remote)
    cmd(f"ping -c 1 -W5 {cfg.remote_addr_v['6']}", ns=cfg.netns)


def main() -> None:
    with NetDrvContEnv(__file__) as cfg:
        ksft_run([test_ping], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
