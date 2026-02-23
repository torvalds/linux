#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import re
from os import path
from lib.py import ksft_run, ksft_exit, KsftSkipEx, ksft_variants, KsftNamedVariant
from lib.py import NetDrvEpEnv
from lib.py import bkg, cmd, defer, ethtool, rand_port, wait_port_listen
from lib.py import EthtoolFamily

SKIP_CODE = 42

def create_rss_ctx(cfg):
    output = ethtool(f"-X {cfg.ifname} context new start {cfg.target} equal 1").stdout
    values = re.search(r'New RSS context is (\d+)', output).group(1)
    return int(values)


def set_flow_rule(cfg):
    output = ethtool(f"-N {cfg.ifname} flow-type tcp6 dst-port {cfg.port} action {cfg.target}").stdout
    values = re.search(r'ID (\d+)', output).group(1)
    return int(values)


def set_flow_rule_rss(cfg, rss_ctx_id):
    output = ethtool(f"-N {cfg.ifname} flow-type tcp6 dst-port {cfg.port} context {rss_ctx_id}").stdout
    values = re.search(r'ID (\d+)', output).group(1)
    return int(values)


def single(cfg):
    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    channels = channels['combined-count']
    if channels < 2:
        raise KsftSkipEx('Test requires NETIF with at least 2 combined channels')

    rings = cfg.ethnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    rx_rings = rings['rx']
    hds_thresh = rings.get('hds-thresh', 0)

    cfg.ethnl.rings_set({'header': {'dev-index': cfg.ifindex},
                         'tcp-data-split': 'enabled',
                         'hds-thresh': 0,
                         'rx': 64})
    defer(cfg.ethnl.rings_set, {'header': {'dev-index': cfg.ifindex},
                                'tcp-data-split': 'unknown',
                                'hds-thresh': hds_thresh,
                                'rx': rx_rings})

    cfg.target = channels - 1
    ethtool(f"-X {cfg.ifname} equal {cfg.target}")
    defer(ethtool, f"-X {cfg.ifname} default")

    flow_rule_id = set_flow_rule(cfg)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")


def rss(cfg):
    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    channels = channels['combined-count']
    if channels < 2:
        raise KsftSkipEx('Test requires NETIF with at least 2 combined channels')

    rings = cfg.ethnl.rings_get({'header': {'dev-index': cfg.ifindex}})
    rx_rings = rings['rx']
    hds_thresh = rings.get('hds-thresh', 0)

    cfg.ethnl.rings_set({'header': {'dev-index': cfg.ifindex},
                         'tcp-data-split': 'enabled',
                         'hds-thresh': 0,
                         'rx': 64})
    defer(cfg.ethnl.rings_set, {'header': {'dev-index': cfg.ifindex},
                                'tcp-data-split': 'unknown',
                                'hds-thresh': hds_thresh,
                                'rx': rx_rings})

    cfg.target = channels - 1
    ethtool(f"-X {cfg.ifname} equal {cfg.target}")
    defer(ethtool, f"-X {cfg.ifname} default")

    rss_ctx_id = create_rss_ctx(cfg)
    defer(ethtool, f"-X {cfg.ifname} delete context {rss_ctx_id}")

    flow_rule_id = set_flow_rule_rss(cfg, rss_ctx_id)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")


@ksft_variants([
    KsftNamedVariant("single", single),
    KsftNamedVariant("rss", rss),
])
def test_zcrx(cfg, setup) -> None:
    cfg.require_ipver('6')

    setup(cfg)
    rx_cmd = f"{cfg.bin_local} -s -p {cfg.port} -i {cfg.ifname} -q {cfg.target}"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {cfg.port} -l 12840"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(cfg.port, proto="tcp")
        cmd(tx_cmd, host=cfg.remote)


@ksft_variants([
    KsftNamedVariant("single", single),
    KsftNamedVariant("rss", rss),
])
def test_zcrx_oneshot(cfg, setup) -> None:
    cfg.require_ipver('6')

    setup(cfg)
    rx_cmd = f"{cfg.bin_local} -s -p {cfg.port} -i {cfg.ifname} -q {cfg.target} -o 4"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {cfg.port} -l 4096 -z 16384"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(cfg.port, proto="tcp")
        cmd(tx_cmd, host=cfg.remote)


def test_zcrx_large_chunks(cfg) -> None:
    """Test zcrx with large buffer chunks."""

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

    rx_cmd = f"{cfg.bin_local} -s -p {port} -i {cfg.ifname} -q {combined_chans - 1} -x 2"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {port} -l 12840"

    probe = cmd(rx_cmd + " -d", fail=False)
    if probe.ret == SKIP_CODE:
        raise KsftSkipEx(probe.stdout)

    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(port, proto="tcp")
        cmd(tx_cmd, host=cfg.remote)


def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        cfg.bin_local = path.abspath(path.dirname(__file__) + "/../../../drivers/net/hw/iou-zcrx")
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

        cfg.ethnl = EthtoolFamily()
        cfg.port = rand_port()
        ksft_run(globs=globals(), cases=[test_zcrx, test_zcrx_oneshot], args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
