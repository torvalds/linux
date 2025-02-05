#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq
from lib.py import NetDrvEpEnv
from lib.py import bkg, cmd, wait_port_listen, rand_port


def test_v4(cfg) -> None:
    cfg.require_v4()

    cmd(f"ping -c 1 -W0.5 {cfg.remote_v4}")
    cmd(f"ping -c 1 -W0.5 {cfg.v4}", host=cfg.remote)


def test_v6(cfg) -> None:
    cfg.require_v6()

    cmd(f"ping -c 1 -W0.5 {cfg.remote_v6}")
    cmd(f"ping -c 1 -W0.5 {cfg.v6}", host=cfg.remote)


def test_tcp(cfg) -> None:
    cfg.require_cmd("socat", remote=True)

    port = rand_port()
    listen_cmd = f"socat -{cfg.addr_ipver} -t 2 -u TCP-LISTEN:{port},reuseport STDOUT"

    with bkg(listen_cmd, exit_wait=True) as nc:
        wait_port_listen(port)

        cmd(f"echo ping | socat -t 2 -u STDIN TCP:{cfg.baddr}:{port}",
            shell=True, host=cfg.remote)
    ksft_eq(nc.stdout.strip(), "ping")

    with bkg(listen_cmd, host=cfg.remote, exit_wait=True) as nc:
        wait_port_listen(port, host=cfg.remote)

        cmd(f"echo ping | socat -t 2 -u STDIN TCP:{cfg.remote_baddr}:{port}", shell=True)
    ksft_eq(nc.stdout.strip(), "ping")


def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
