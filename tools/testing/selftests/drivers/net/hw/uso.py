#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Test USO

Sends large UDP datagrams with UDP_SEGMENT and verifies that the peer
receives the expected total payload and that the NIC transmitted at least
the expected number of segments.
"""
import random
import socket
import string

from lib.py import ksft_run, ksft_exit, KsftSkipEx
from lib.py import ksft_eq, ksft_ge, ksft_variants, KsftNamedVariant
from lib.py import NetDrvEpEnv
from lib.py import bkg, defer, ethtool, ip, rand_port, wait_port_listen

# python doesn't expose this constant, so we need to hardcode it to enable UDP
# segmentation for large payloads
UDP_SEGMENT = 103


def _send_uso(cfg, ipver, mss, total_payload, port):
    if ipver == "4":
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        dst = (cfg.remote_addr_v["4"], port)
    else:
        sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        dst = (cfg.remote_addr_v["6"], port)

    sock.setsockopt(socket.IPPROTO_UDP, UDP_SEGMENT, mss)
    payload = ''.join(random.choice(string.ascii_lowercase)
                      for _ in range(total_payload))
    sock.sendto(payload.encode(), dst)
    sock.close()


def _get_tx_packets(cfg):
    stats = ip(f"-s link show dev {cfg.ifname}", json=True)[0]
    return stats['stats64']['tx']['packets']


def _test_uso(cfg, ipver, mss, total_payload):
    cfg.require_ipver(ipver)
    cfg.require_cmd("socat", remote=True)

    features = ethtool(f"-k {cfg.ifname}", json=True)
    uso_was_on = features[0]["tx-udp-segmentation"]["active"]

    try:
        ethtool(f"-K {cfg.ifname} tx-udp-segmentation on")
    except Exception as exc:
        raise KsftSkipEx(
            "Device does not support tx-udp-segmentation") from exc
    if not uso_was_on:
        defer(ethtool, f"-K {cfg.ifname} tx-udp-segmentation off")

    expected_segs = (total_payload + mss - 1) // mss

    port = rand_port(stype=socket.SOCK_DGRAM)
    rx_cmd = f"socat -{ipver} -T 2 -u UDP-LISTEN:{port},reuseport STDOUT"

    tx_before = _get_tx_packets(cfg)

    with bkg(rx_cmd, host=cfg.remote, exit_wait=True) as rx:
        wait_port_listen(port, proto="udp", host=cfg.remote)
        _send_uso(cfg, ipver, mss, total_payload, port)

    ksft_eq(len(rx.stdout), total_payload,
            comment=f"Received {len(rx.stdout)}B, expected {total_payload}B")

    cfg.wait_hw_stats_settle()

    tx_after = _get_tx_packets(cfg)
    tx_delta = tx_after - tx_before

    ksft_ge(tx_delta, expected_segs,
            comment=f"Expected >= {expected_segs} tx packets, got {tx_delta}")


def _uso_variants():
    for ipver in ["4", "6"]:
        yield KsftNamedVariant(f"v{ipver}_partial", ipver, 1400, 1400 * 10 + 500)
        yield KsftNamedVariant(f"v{ipver}_exact", ipver, 1400, 1400 * 5)


@ksft_variants(_uso_variants())
def test_uso(cfg, ipver, mss, total_payload):
    """Send a USO datagram and verify the peer receives the expected segments."""
    _test_uso(cfg, ipver, mss, total_payload)


def main() -> None:
    """Run USO tests."""
    with NetDrvEpEnv(__file__) as cfg:
        ksft_run([test_uso],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
