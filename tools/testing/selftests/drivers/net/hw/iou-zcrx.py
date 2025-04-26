#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import re
from os import path
from lib.py import ksft_run, ksft_exit
from lib.py import NetDrvEpEnv
from lib.py import bkg, cmd, defer, ethtool, wait_port_listen


def _get_current_settings(cfg):
    output = ethtool(f"-g {cfg.ifname}", host=cfg.remote).stdout
    rx_ring = re.findall(r'RX:\s+(\d+)', output)
    hds_thresh = re.findall(r'HDS thresh:\s+(\d+)', output)
    return (int(rx_ring[1]), int(hds_thresh[1]))


def _get_combined_channels(cfg):
    output = ethtool(f"-l {cfg.ifname}", host=cfg.remote).stdout
    values = re.findall(r'Combined:\s+(\d+)', output)
    return int(values[1])


def _create_rss_ctx(cfg, chans):
    output = ethtool(f"-X {cfg.ifname} context new start {chans - 1} equal 1", host=cfg.remote).stdout
    values = re.search(r'New RSS context is (\d+)', output).group(1)
    ctx_id = int(values)
    return (ctx_id, defer(ethtool, f"-X {cfg.ifname} delete context {ctx_id}", host=cfg.remote))


def _set_flow_rule(cfg, chan):
    output = ethtool(f"-N {cfg.ifname} flow-type tcp6 dst-port 9999 action {chan}", host=cfg.remote).stdout
    values = re.search(r'ID (\d+)', output).group(1)
    return int(values)


def _set_flow_rule_rss(cfg, chan):
    output = ethtool(f"-N {cfg.ifname} flow-type tcp6 dst-port 9999 action {chan}", host=cfg.remote).stdout
    values = re.search(r'ID (\d+)', output).group(1)
    return int(values)


def test_zcrx(cfg) -> None:
    cfg.require_ipver('6')

    combined_chans = _get_combined_channels(cfg)
    if combined_chans < 2:
        raise KsftSkipEx('at least 2 combined channels required')
    (rx_ring, hds_thresh) = _get_current_settings(cfg)

    ethtool(f"-G {cfg.ifname} tcp-data-split on", host=cfg.remote)
    defer(ethtool, f"-G {cfg.ifname} tcp-data-split auto", host=cfg.remote)
    ethtool(f"-G {cfg.ifname} hds-thresh 0", host=cfg.remote)
    defer(ethtool, f"-G {cfg.ifname} hds-thresh {hds_thresh}", host=cfg.remote)
    ethtool(f"-G {cfg.ifname} rx 64", host=cfg.remote)
    defer(ethtool, f"-G {cfg.ifname} rx {rx_ring}", host=cfg.remote)
    ethtool(f"-X {cfg.ifname} equal {combined_chans - 1}", host=cfg.remote)
    defer(ethtool, f"-X {cfg.ifname} default", host=cfg.remote)
    flow_rule_id = _set_flow_rule(cfg, combined_chans - 1)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}", host=cfg.remote)

    rx_cmd = f"{cfg.bin_remote} -s -p 9999 -i {cfg.ifname} -q {combined_chans - 1}"
    tx_cmd = f"{cfg.bin_local} -c -h {cfg.remote_addr_v['6']} -p 9999 -l 12840"
    with bkg(rx_cmd, host=cfg.remote, exit_wait=True):
        wait_port_listen(9999, proto="tcp", host=cfg.remote)
        cmd(tx_cmd)


def test_zcrx_oneshot(cfg) -> None:
    cfg.require_ipver('6')

    combined_chans = _get_combined_channels(cfg)
    if combined_chans < 2:
        raise KsftSkipEx('at least 2 combined channels required')
    (rx_ring, hds_thresh) = _get_current_settings(cfg)

    ethtool(f"-G {cfg.ifname} tcp-data-split on", host=cfg.remote)
    defer(ethtool, f"-G {cfg.ifname} tcp-data-split auto", host=cfg.remote)
    ethtool(f"-G {cfg.ifname} hds-thresh 0", host=cfg.remote)
    defer(ethtool, f"-G {cfg.ifname} hds-thresh {hds_thresh}", host=cfg.remote)
    ethtool(f"-G {cfg.ifname} rx 64", host=cfg.remote)
    defer(ethtool, f"-G {cfg.ifname} rx {rx_ring}", host=cfg.remote)
    ethtool(f"-X {cfg.ifname} equal {combined_chans - 1}", host=cfg.remote)
    defer(ethtool, f"-X {cfg.ifname} default", host=cfg.remote)
    flow_rule_id = _set_flow_rule(cfg, combined_chans - 1)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}", host=cfg.remote)

    rx_cmd = f"{cfg.bin_remote} -s -p 9999 -i {cfg.ifname} -q {combined_chans - 1} -o 4"
    tx_cmd = f"{cfg.bin_local} -c -h {cfg.remote_addr_v['6']} -p 9999 -l 4096 -z 16384"
    with bkg(rx_cmd, host=cfg.remote, exit_wait=True):
        wait_port_listen(9999, proto="tcp", host=cfg.remote)
        cmd(tx_cmd)


def test_zcrx_rss(cfg) -> None:
    cfg.require_ipver('6')

    combined_chans = _get_combined_channels(cfg)
    if combined_chans < 2:
        raise KsftSkipEx('at least 2 combined channels required')
    (rx_ring, hds_thresh) = _get_current_settings(cfg)

    ethtool(f"-G {cfg.ifname} tcp-data-split on", host=cfg.remote)
    defer(ethtool, f"-G {cfg.ifname} tcp-data-split auto", host=cfg.remote)
    ethtool(f"-G {cfg.ifname} hds-thresh 0", host=cfg.remote)
    defer(ethtool, f"-G {cfg.ifname} hds-thresh {hds_thresh}", host=cfg.remote)
    ethtool(f"-G {cfg.ifname} rx 64", host=cfg.remote)
    defer(ethtool, f"-G {cfg.ifname} rx {rx_ring}", host=cfg.remote)
    ethtool(f"-X {cfg.ifname} equal {combined_chans - 1}", host=cfg.remote)
    defer(ethtool, f"-X {cfg.ifname} default", host=cfg.remote)

    (ctx_id, delete_ctx) = _create_rss_ctx(cfg, combined_chans)
    flow_rule_id = _set_flow_rule_rss(cfg, ctx_id)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}", host=cfg.remote)

    rx_cmd = f"{cfg.bin_remote} -s -p 9999 -i {cfg.ifname} -q {combined_chans - 1}"
    tx_cmd = f"{cfg.bin_local} -c -h {cfg.remote_addr_v['6']} -p 9999 -l 12840"
    with bkg(rx_cmd, host=cfg.remote, exit_wait=True):
        wait_port_listen(9999, proto="tcp", host=cfg.remote)
        cmd(tx_cmd)


def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        cfg.bin_local = path.abspath(path.dirname(__file__) + "/../../../drivers/net/hw/iou-zcrx")
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
