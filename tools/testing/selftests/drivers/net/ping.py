#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_run, ksft_exit
from lib.py import NetDrvEpEnv
from lib.py import cmd


def test_v4(cfg) -> None:
    cmd(f"ping -c 1 -W0.5 {cfg.remote_v4}")
    cmd(f"ping -c 1 -W0.5 {cfg.v4}", host=cfg.remote)


def test_v6(cfg) -> None:
    cmd(f"ping -c 1 -W0.5 {cfg.remote_v6}")
    cmd(f"ping -c 1 -W0.5 {cfg.v6}", host=cfg.remote)


def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
