#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import re
import resource
import time
from os import path
from lib.py import ksft_run, ksft_exit, KsftSkipEx, ksft_variants, KsftNamedVariant
from lib.py import NetDrvEpEnv
from lib.py import bkg, cmd, defer, ethtool, rand_port, wait_port_listen
from lib.py import EthtoolFamily, NetdevFamily

SKIP_CODE = 42


def mp_clear_wait(cfg):
    """Wait for io_uring memory providers to clear from all device queues."""
    deadline = time.time() + 5
    while time.time() < deadline:
        queues = cfg.netnl.queue_get({'ifindex': cfg.ifindex}, dump=True)
        if not any('io-uring' in q for q in queues):
            return
        time.sleep(0.1)
    raise TimeoutError("Timed out waiting for memory provider to clear")


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


def check_iou_rx_buf_len(cfg, expected_rx_buf_len):
    """Check the io-uring memory provider exposes the expected rx_buf_len."""
    q = cfg.netnl.queue_get({'ifindex': cfg.ifindex, 'type': 'rx', 'id': cfg.target})
    napi_id = q['napi-id']
    pools = cfg.netnl.page_pool_get({}, dump=True)
    pools = [p for p in pools if p.get('napi-id') == napi_id
             and 'io-uring' in p]
    if len(pools) != 1:
        raise Exception(f"Expected 1 io-uring page pool, found {len(pools)}")
    rx_buf_len = pools[0]['io-uring'].get('rx-buf-len')
    if rx_buf_len is None:
        raise KsftSkipEx("io-uring 'rx-buf-len' attribute not supported")
    if rx_buf_len != expected_rx_buf_len:
        raise Exception(f'Expected io-uring rx-buf-len {expected_rx_buf_len}, '
                        f'got {rx_buf_len}')


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
    defer(mp_clear_wait, cfg)

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
    defer(mp_clear_wait, cfg)

    cfg.target = channels - 1
    ethtool(f"-X {cfg.ifname} equal {cfg.target}")
    defer(ethtool, f"-X {cfg.ifname} default")

    rss_ctx_id = create_rss_ctx(cfg)
    defer(ethtool, f"-X {cfg.ifname} delete context {rss_ctx_id}")

    flow_rule_id = set_flow_rule_rss(cfg, rss_ctx_id)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")


def _require_ntuple(cfg):
    features = ethtool(f"-k {cfg.ifname}", json=True)[0]
    if not features["ntuple-filters"]["active"]:
        if features["ntuple-filters"]["fixed"]:
            raise KsftSkipEx("Device does not support ntuple-filters")
        ethtool(f"-K {cfg.ifname} ntuple-filters on")
        defer(ethtool, f"-K {cfg.ifname} ntuple-filters off")


@ksft_variants([
    KsftNamedVariant("single", single),
    KsftNamedVariant("rss", rss),
])
def test_zcrx(cfg, setup) -> None:
    cfg.require_ipver('6')
    _require_ntuple(cfg)

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
    _require_ntuple(cfg)

    setup(cfg)
    rx_cmd = f"{cfg.bin_local} -s -p {cfg.port} -i {cfg.ifname} -q {cfg.target} -o 4"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {cfg.port} -l 4096 -z 16384"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(cfg.port, proto="tcp")
        cmd(tx_cmd, host=cfg.remote)


def test_zcrx_large_chunks(cfg) -> None:
    """Test zcrx with large buffer chunks."""

    cfg.require_ipver('6')
    _require_ntuple(cfg)

    hp_file = "/proc/sys/vm/nr_hugepages"
    with open(hp_file, 'r+', encoding='utf-8') as f:
        nr_hugepages = int(f.read().strip())
        if nr_hugepages < 64:
            f.seek(0)
            f.write("64")
            defer(lambda: open(hp_file, 'w', encoding='utf-8').write(str(nr_hugepages)))

    single(cfg)
    page_size = resource.getpagesize()
    nr_pages = 2
    rx_buf_len = nr_pages * page_size
    rx_cmd = f"{cfg.bin_local} -s -p {cfg.port} -i {cfg.ifname} -q {cfg.target} -x {nr_pages}"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {cfg.port} -l 12840"

    probe = cmd(rx_cmd + " -d", fail=False)
    if probe.ret == SKIP_CODE:
        raise KsftSkipEx(probe.stdout.strip())

    mp_clear_wait(cfg)
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(cfg.port, proto="tcp")

        check_iou_rx_buf_len(cfg, rx_buf_len)

        cmd(tx_cmd, host=cfg.remote)


def main() -> None:
    with NetDrvEpEnv(__file__) as cfg:
        cfg.bin_local = path.abspath(path.dirname(__file__) + "/../../../drivers/net/hw/iou-zcrx")
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

        cfg.ethnl = EthtoolFamily()
        cfg.netnl = NetdevFamily()
        cfg.port = rand_port()
        ksft_run(globs=globals(), cases=[test_zcrx, test_zcrx_oneshot,
                                        test_zcrx_large_chunks], args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
