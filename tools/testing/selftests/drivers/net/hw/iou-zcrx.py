#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import re
from os import path
from lib.py import ksft_run, ksft_exit
from lib.py import NetDrvEpEnv
from lib.py import bkg, cmd, ethtool, wait_port_listen


def _get_rx_ring_entries(cfg):
    output = ethtool(f"-g {cfg.ifname}", host=cfg.remote).stdout
    values = re.findall(r'RX:\s+(\d+)', output)
    return int(values[1])


def _get_combined_channels(cfg):
    output = ethtool(f"-l {cfg.ifname}", host=cfg.remote).stdout
    values = re.findall(r'Combined:\s+(\d+)', output)
    return int(values[1])


def _set_flow_rule(cfg, chan):
    output = ethtool(f"-N {cfg.ifname} flow-type tcp6 dst-port 9999 action {chan}", host=cfg.remote).stdout
    values = re.search(r'ID (\d+)', output).group(1)
    return int(values)


def test_zcrx(cfg) -> None:
    cfg.require_ipver('6')

    combined_chans = _get_combined_channels(cfg)
    if combined_chans < 2:
        raise KsftSkipEx('at least 2 combined channels required')
    rx_ring = _get_rx_ring_entries(cfg)

    try:
        ethtool(f"-G {cfg.ifname} tcp-data-split on", host=cfg.remote)
        ethtool(f"-G {cfg.ifname} rx 64", host=cfg.remote)
        ethtool(f"-X {cfg.ifname} equal {combined_chans - 1}", host=cfg.remote)
        flow_rule_id = _set_flow_rule(cfg, combined_chans - 1)

        rx_cmd = f"{cfg.bin_remote} -s -p 9999 -i {cfg.ifname} -q {combined_chans - 1}"
        tx_cmd = f"{cfg.bin_local} -c -h {cfg.remote_addr_v['6']} -p 9999 -l 12840"
        with bkg(rx_cmd, host=cfg.remote, exit_wait=True):
            wait_port_listen(9999, proto="tcp", host=cfg.remote)
            cmd(tx_cmd)
    finally:
        ethtool(f"-N {cfg.ifname} delete {flow_rule_id}", host=cfg.remote)
        ethtool(f"-X {cfg.ifname} default", host=cfg.remote)
        ethtool(f"-G {cfg.ifname} rx {rx_ring}", host=cfg.remote)
        ethtool(f"-G {cfg.ifname} tcp-data-split auto", host=cfg.remote)


def test_zcrx_oneshot(cfg) -> None:
    cfg.require_ipver('6')

    combined_chans = _get_combined_channels(cfg)
    if combined_chans < 2:
        raise KsftSkipEx('at least 2 combined channels required')
    rx_ring = _get_rx_ring_entries(cfg)

    try:
        ethtool(f"-G {cfg.ifname} tcp-data-split on", host=cfg.remote)
        ethtool(f"-G {cfg.ifname} rx 64", host=cfg.remote)
        ethtool(f"-X {cfg.ifname} equal {combined_chans - 1}", host=cfg.remote)
        flow_rule_id = _set_flow_rule(cfg, combined_chans - 1)

        rx_cmd = f"{cfg.bin_remote} -s -p 9999 -i {cfg.ifname} -q {combined_chans - 1} -o 4"
        tx_cmd = f"{cfg.bin_local} -c -h {cfg.remote_addr_v['6']} -p 9999 -l 4096 -z 16384"
        with bkg(rx_cmd, host=cfg.remote, exit_wait=True):
            wait_port_listen(9999, proto="tcp", host=cfg.remote)
            cmd(tx_cmd)
    finally:
        ethtool(f"-N {cfg.ifname} delete {flow_rule_id}", host=cfg.remote)
        ethtool(f"-X {cfg.ifname} default", host=cfg.remote)
        ethtool(f"-G {cfg.ifname} rx {rx_ring}", host=cfg.remote)
        ethtool(f"-G {cfg.ifname} tcp-data-split auto", host=cfg.remote)


def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        cfg.bin_local = path.abspath(path.dirname(__file__) + "/../../../drivers/net/hw/iou-zcrx")
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
