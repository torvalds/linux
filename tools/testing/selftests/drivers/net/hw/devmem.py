#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from os import path
from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, KsftSkipEx
from lib.py import NetDrvEpEnv
from lib.py import bkg, cmd, rand_port, wait_port_listen
from lib.py import ksft_disruptive


def require_devmem(cfg):
    if not hasattr(cfg, "_devmem_probed"):
        probe_command = f"{cfg.bin_local} -f {cfg.ifname}"
        cfg._devmem_supported = cmd(probe_command, fail=False, shell=True).ret == 0
        cfg._devmem_probed = True

    if not cfg._devmem_supported:
        raise KsftSkipEx("Test requires devmem support")


@ksft_disruptive
def check_rx(cfg) -> None:
    require_devmem(cfg)

    port = rand_port()
    socat = f"socat -u - TCP{cfg.addr_ipver}:{cfg.baddr}:{port},bind={cfg.remote_baddr}:{port}"
    listen_cmd = f"{cfg.bin_local} -l -f {cfg.ifname} -s {cfg.addr} -p {port} -c {cfg.remote_addr} -v 7"

    with bkg(listen_cmd, exit_wait=True) as ncdevmem:
        wait_port_listen(port)
        cmd(f"yes $(echo -e \x01\x02\x03\x04\x05\x06) | \
            head -c 1K | {socat}", host=cfg.remote, shell=True)

    ksft_eq(ncdevmem.ret, 0)


@ksft_disruptive
def check_tx(cfg) -> None:
    require_devmem(cfg)

    port = rand_port()
    listen_cmd = f"socat -U - TCP{cfg.addr_ipver}-LISTEN:{port}"

    with bkg(listen_cmd, host=cfg.remote, exit_wait=True) as socat:
        wait_port_listen(port, host=cfg.remote)
        cmd(f"echo -e \"hello\\nworld\"| {cfg.bin_local} -f {cfg.ifname} -s {cfg.remote_addr} -p {port}", shell=True)

    ksft_eq(socat.stdout.strip(), "hello\nworld")


@ksft_disruptive
def check_tx_chunks(cfg) -> None:
    require_devmem(cfg)

    port = rand_port()
    listen_cmd = f"socat -U - TCP{cfg.addr_ipver}-LISTEN:{port}"

    with bkg(listen_cmd, host=cfg.remote, exit_wait=True) as socat:
        wait_port_listen(port, host=cfg.remote)
        cmd(f"echo -e \"hello\\nworld\"| {cfg.bin_local} -f {cfg.ifname} -s {cfg.remote_addr} -p {port} -z 3", shell=True)

    ksft_eq(socat.stdout.strip(), "hello\nworld")


def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        cfg.bin_local = path.abspath(path.dirname(__file__) + "/ncdevmem")
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

        ksft_run([check_rx, check_tx, check_tx_chunks],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
