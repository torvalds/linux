#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Test ethtool NFC (ntuple) flow steering rules."""

import random
from enum import Enum, auto
from lib.py import ksft_run, ksft_exit
from lib.py import ksft_eq, ksft_ge
from lib.py import ksft_variants, KsftNamedVariant
from lib.py import EthtoolFamily, NetDrvEpEnv, NetdevFamily
from lib.py import KsftSkipEx
from lib.py import cmd, ethtool, defer, rand_ports, bkg, wait_port_listen


class NtupleField(Enum):
    SRC_IP = auto()
    DST_IP = auto()
    SRC_PORT = auto()
    DST_PORT = auto()


def _require_ntuple(cfg):
    features = ethtool(f"-k {cfg.ifname}", json=True)[0]
    if not features["ntuple-filters"]["active"]:
        raise KsftSkipEx("Ntuple filters not enabled on the device: " + str(features["ntuple-filters"]))


def _get_rx_cnts(cfg, prev=None):
    """Get Rx packet counts for all queues, as a simple list of integers
       if @prev is specified the prev counts will be subtracted"""
    cfg.wait_hw_stats_settle()
    data = cfg.netdevnl.qstats_get({"ifindex": cfg.ifindex, "scope": ["queue"]}, dump=True)
    data = [x for x in data if x['queue-type'] == "rx"]
    max_q = max([x["queue-id"] for x in data])
    queue_stats = [0] * (max_q + 1)
    for q in data:
        queue_stats[q["queue-id"]] = q["rx-packets"]
        if prev and q["queue-id"] < len(prev):
            queue_stats[q["queue-id"]] -= prev[q["queue-id"]]
    return queue_stats


def _ntuple_rule_add(cfg, flow_spec):
    """Install an NFC rule via ethtool."""

    output = ethtool(f"-N {cfg.ifname} {flow_spec}").stdout
    rule_id = int(output.split()[-1])
    defer(ethtool, f"-N {cfg.ifname} delete {rule_id}")


def _setup_isolated_queue(cfg):
    """Default all traffic to queue 0, and pick a random queue to
       steer NFC traffic to."""

    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    ch_max = channels['combined-max']
    qcnt = channels['combined-count']

    if ch_max < 2:
        raise KsftSkipEx(f"Need at least 2 combined channels, max is {ch_max}")

    desired_queues = min(ch_max, 4)
    if qcnt >= desired_queues:
        desired_queues = qcnt
    else:
        ethtool(f"-L {cfg.ifname} combined {desired_queues}")
        defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")

    ethtool(f"-X {cfg.ifname} equal 1")
    defer(ethtool, f"-X {cfg.ifname} default")

    return random.randint(1, desired_queues - 1)


def _send_traffic(cfg, ipver, proto, dst_port, src_port, pkt_cnt=40):
    """Generate traffic with the desired flow signature."""

    cfg.require_cmd("socat", remote=True)

    socat_proto = proto.upper()
    dst_addr = f"[{cfg.addr_v['6']}]" if ipver == '6' else cfg.addr_v['4']

    extra_opts = ",nodelay" if proto == "tcp" else ",shut-null"

    listen_cmd = (f"socat -{ipver} -t 2 -u "
                  f"{socat_proto}-LISTEN:{dst_port},reuseport /dev/null")
    with bkg(listen_cmd, exit_wait=True):
        wait_port_listen(dst_port, proto=proto)
        send_cmd = f"""
        bash -c 'for i in $(seq {pkt_cnt}); do echo msg; sleep 0.02; done' |
        socat -{ipver} -u - \
            {socat_proto}:{dst_addr}:{dst_port},sourceport={src_port},reuseaddr{extra_opts}
        """
        cmd(send_cmd, shell=True, host=cfg.remote)


def _add_ntuple_rule_and_send_traffic(cfg, ipver, proto, fields, test_queue):
    ports = rand_ports(2)
    src_port = ports[0]
    dst_port = ports[1]
    flow_parts = [f"flow-type {proto}{ipver}"]

    for field in fields:
        if field == NtupleField.SRC_IP:
            flow_parts.append(f"src-ip {cfg.remote_addr_v[ipver]}")
        elif field == NtupleField.DST_IP:
            flow_parts.append(f"dst-ip {cfg.addr_v[ipver]}")
        elif field == NtupleField.SRC_PORT:
            flow_parts.append(f"src-port {src_port}")
        elif field == NtupleField.DST_PORT:
            flow_parts.append(f"dst-port {dst_port}")

    flow_parts.append(f"action {test_queue}")
    _ntuple_rule_add(cfg, " ".join(flow_parts))
    _send_traffic(cfg, ipver, proto, dst_port=dst_port, src_port=src_port)


def _ntuple_variants():
    for ipver in ["4", "6"]:
        for proto in ["tcp", "udp"]:
            for fields in [[NtupleField.SRC_IP],
                           [NtupleField.DST_IP],
                           [NtupleField.SRC_PORT],
                           [NtupleField.DST_PORT],
                           [NtupleField.SRC_IP, NtupleField.DST_IP],
                           [NtupleField.SRC_IP, NtupleField.DST_IP,
                            NtupleField.SRC_PORT, NtupleField.DST_PORT]]:
                name = ".".join(f.name.lower() for f in fields)
                yield KsftNamedVariant(f"{proto}{ipver}.{name}",
                                      ipver, proto, fields)


@ksft_variants(_ntuple_variants())
def queue(cfg, ipver, proto, fields):
    """Test that an NFC rule steers traffic to the correct queue."""

    cfg.require_ipver(ipver)
    _require_ntuple(cfg)

    test_queue = _setup_isolated_queue(cfg)

    cnts = _get_rx_cnts(cfg)
    _add_ntuple_rule_and_send_traffic(cfg, ipver, proto, fields, test_queue)
    cnts = _get_rx_cnts(cfg, prev=cnts)

    ksft_ge(cnts[test_queue], 40, f"Traffic on test queue {test_queue}: {cnts}")
    sum_idle = sum(cnts) - cnts[0] - cnts[test_queue]
    ksft_eq(sum_idle, 0, f"Traffic on idle queues: {cnts}")


def main() -> None:
    """Ksft boilerplate main."""

    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        cfg.ethnl = EthtoolFamily()
        cfg.netdevnl = NetdevFamily()
        ksft_run([queue], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
