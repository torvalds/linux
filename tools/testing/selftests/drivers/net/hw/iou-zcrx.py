#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import re
from os import path
from lib.py import ksft_run, ksft_exit, KsftSkipEx
from lib.py import NetDrvEpEnv
from lib.py import bkg, cmd, defer, ethtool, rand_port, wait_port_listen


def _get_current_settings(cfg):
    output = ethtool(f"-g {cfg.ifname}", json=True)[0]
    return (output['rx'], output['hds-thresh'])


def _get_combined_channels(cfg):
    output = ethtool(f"-l {cfg.ifname}").stdout
    values = re.findall(r'Combined:\s+(\d+)', output)
    return int(values[1])


def _create_rss_ctx(cfg, chan):
    output = ethtool(f"-X {cfg.ifname} context new start {chan} equal 1").stdout
    values = re.search(r'New RSS context is (\d+)', output).group(1)
    ctx_id = int(values)
    return (ctx_id, defer(ethtool, f"-X {cfg.ifname} delete context {ctx_id}"))


def _set_flow_rule(cfg, port, chan):
    output = ethtool(f"-N {cfg.ifname} flow-type tcp6 dst-port {port} action {chan}").stdout
    values = re.search(r'ID (\d+)', output).group(1)
    return int(values)


def _set_flow_rule_rss(cfg, port, ctx_id):
    output = ethtool(f"-N {cfg.ifname} flow-type tcp6 dst-port {port} context {ctx_id}").stdout
    values = re.search(r'ID (\d+)', output).group(1)
    return int(values)


def test_zcrx(cfg) -> None:
    cfg.require_ipver('6')

    combined_chans = _get_combined_channels(cfg)
    if combined_chans < 2:
        raise KsftSkipEx('at least 2 combined channels required')
    (rx_ring, hds_thresh) = _get_current_settings(cfg)
    port = rand_port()

    ethtool(f"-G {cfg.ifname} tcp-data-split on")
    defer(ethtool, f"-G {cfg.ifname} tcp-data-split auto")

    ethtool(f"-G {cfg.ifname} hds-thresh 0")
    defer(ethtool, f"-G {cfg.ifname} hds-thresh {hds_thresh}")

    ethtool(f"-G {cfg.ifname} rx 64")
    defer(ethtool, f"-G {cfg.ifname} rx {rx_ring}")

    ethtool(f"-X {cfg.ifname} equal {combined_chans - 1}")
    defer(ethtool, f"-X {cfg.ifname} default")

    flow_rule_id = _set_flow_rule(cfg, port, combined_chans - 1)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

    rx_cmd = f"{cfg.bin_local} -s -p {port} -i {cfg.ifname} -q {combined_chans - 1}"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {port} -l 12840"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(port, proto="tcp")
        cmd(tx_cmd, host=cfg.remote)


def test_zcrx_oneshot(cfg) -> None:
    cfg.require_ipver('6')

    combined_chans = _get_combined_channels(cfg)
    if combined_chans < 2:
        raise KsftSkipEx('at least 2 combined channels required')
    (rx_ring, hds_thresh) = _get_current_settings(cfg)
    port = rand_port()

    ethtool(f"-G {cfg.ifname} tcp-data-split on")
    defer(ethtool, f"-G {cfg.ifname} tcp-data-split auto")

    ethtool(f"-G {cfg.ifname} hds-thresh 0")
    defer(ethtool, f"-G {cfg.ifname} hds-thresh {hds_thresh}")

    ethtool(f"-G {cfg.ifname} rx 64")
    defer(ethtool, f"-G {cfg.ifname} rx {rx_ring}")

    ethtool(f"-X {cfg.ifname} equal {combined_chans - 1}")
    defer(ethtool, f"-X {cfg.ifname} default")

    flow_rule_id = _set_flow_rule(cfg, port, combined_chans - 1)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

    rx_cmd = f"{cfg.bin_local} -s -p {port} -i {cfg.ifname} -q {combined_chans - 1} -o 4"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {port} -l 4096 -z 16384"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(port, proto="tcp")
        cmd(tx_cmd, host=cfg.remote)


def test_zcrx_rss(cfg) -> None:
    cfg.require_ipver('6')

    combined_chans = _get_combined_channels(cfg)
    if combined_chans < 2:
        raise KsftSkipEx('at least 2 combined channels required')
    (rx_ring, hds_thresh) = _get_current_settings(cfg)
    port = rand_port()

    ethtool(f"-G {cfg.ifname} tcp-data-split on")
    defer(ethtool, f"-G {cfg.ifname} tcp-data-split auto")

    ethtool(f"-G {cfg.ifname} hds-thresh 0")
    defer(ethtool, f"-G {cfg.ifname} hds-thresh {hds_thresh}")

    ethtool(f"-G {cfg.ifname} rx 64")
    defer(ethtool, f"-G {cfg.ifname} rx {rx_ring}")

    ethtool(f"-X {cfg.ifname} equal {combined_chans - 1}")
    defer(ethtool, f"-X {cfg.ifname} default")

    (ctx_id, delete_ctx) = _create_rss_ctx(cfg, combined_chans - 1)
    flow_rule_id = _set_flow_rule_rss(cfg, port, ctx_id)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

    rx_cmd = f"{cfg.bin_local} -s -p {port} -i {cfg.ifname} -q {combined_chans - 1}"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {port} -l 12840"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(port, proto="tcp")
        cmd(tx_cmd, host=cfg.remote)


def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        cfg.bin_local = path.abspath(path.dirname(__file__) + "/../../../drivers/net/hw/iou-zcrx")
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

        ksft_run(globs=globals(), case_pfx={"test_"}, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
