#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, NetDrvEpEnv
from lib.py import bkg, cmd, rand_port, NetNSEnter

def test_napi_id(cfg) -> None:
    port = rand_port()
    listen_cmd = f"{cfg.test_dir}/napi_id_helper {cfg.addr} {port}"

    with bkg(listen_cmd, ksft_wait=3) as server:
        cmd(f"echo a | socat - TCP:{cfg.baddr}:{port}", host=cfg.remote, shell=True)

    ksft_eq(0, server.ret)

def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        ksft_run([test_napi_id], args=(cfg,))
    ksft_exit()

if __name__ == "__main__":
    main()
