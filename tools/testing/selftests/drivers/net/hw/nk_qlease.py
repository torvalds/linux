#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import re
from os import path
from lib.py import ksft_run, ksft_exit
from lib.py import NetDrvContEnv
from lib.py import bkg, cmd, defer, ethtool, rand_port, wait_port_listen


def create_rss_ctx(cfg):
    output = ethtool(f"-X {cfg.ifname} context new start {cfg.src_queue} equal 1").stdout
    values = re.search(r'New RSS context is (\d+)', output).group(1)
    return int(values)


def set_flow_rule(cfg):
    output = ethtool(f"-N {cfg.ifname} flow-type tcp6 dst-port {cfg.port} action {cfg.src_queue}").stdout
    values = re.search(r'ID (\d+)', output).group(1)
    return int(values)


def set_flow_rule_rss(cfg, rss_ctx_id):
    output = ethtool(f"-N {cfg.ifname} flow-type tcp6 dst-port {cfg.port} context {rss_ctx_id}").stdout
    values = re.search(r'ID (\d+)', output).group(1)
    return int(values)


def test_iou_zcrx(cfg) -> None:
    cfg.require_ipver('6')

    ethtool(f"-X {cfg.ifname} equal {cfg.src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    flow_rule_id = set_flow_rule(cfg)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

    rx_cmd = f"ip netns exec {cfg.netns.name} {cfg.bin_local} -s -p {cfg.port} -i {cfg._nk_guest_ifname} -q {cfg.nk_queue}"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.nk_guest_ipv6} -p {cfg.port} -l 12840"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(cfg.port, proto="tcp", ns=cfg.netns)
        cmd(tx_cmd, host=cfg.remote)


def main() -> None:
    with NetDrvContEnv(__file__, lease=True) as cfg:
        cfg.bin_local = path.abspath(path.dirname(__file__) + "/../../../drivers/net/hw/iou-zcrx")
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)
        cfg.port = rand_port()
        ksft_run([test_iou_zcrx], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
