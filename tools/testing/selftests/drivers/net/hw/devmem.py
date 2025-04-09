#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, KsftSkipEx
from lib.py import NetDrvEpEnv
from lib.py import bkg, cmd, rand_port, wait_port_listen
from lib.py import ksft_disruptive


def require_devmem(cfg):
    if not hasattr(cfg, "_devmem_probed"):
        port = rand_port()
        probe_command = f"./ncdevmem -f {cfg.ifname}"
        cfg._devmem_supported = cmd(probe_command, fail=False, shell=True).ret == 0
        cfg._devmem_probed = True

    if not cfg._devmem_supported:
        raise KsftSkipEx("Test requires devmem support")


@ksft_disruptive
def check_rx(cfg) -> None:
    cfg.require_ipver("6")
    require_devmem(cfg)

    port = rand_port()
    listen_cmd = f"./ncdevmem -l -f {cfg.ifname} -s {cfg.addr_v['6']} -p {port}"

    with bkg(listen_cmd) as socat:
        wait_port_listen(port)
        cmd(f"echo -e \"hello\\nworld\"| socat -u - TCP6:[{cfg.addr_v['6']}]:{port}", host=cfg.remote, shell=True)

    ksft_eq(socat.stdout.strip(), "hello\nworld")


def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        ksft_run([check_rx],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
