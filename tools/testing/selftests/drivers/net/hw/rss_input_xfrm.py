#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import multiprocessing
import socket
from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_ge, cmd, fd_read_timeout
from lib.py import NetDrvEpEnv
from lib.py import EthtoolFamily, NetdevFamily
from lib.py import KsftSkipEx, KsftFailEx
from lib.py import rand_port


def traffic(cfg, local_port, remote_port, ipver):
    af_inet = socket.AF_INET if ipver == "4" else socket.AF_INET6
    sock = socket.socket(af_inet, socket.SOCK_DGRAM)
    sock.bind(("", local_port))
    sock.connect((cfg.remote_addr_v[ipver], remote_port))
    tgt = f"{ipver}:[{cfg.addr_v[ipver]}]:{local_port},sourceport={remote_port}"
    cmd("echo a | socat - UDP" + tgt, host=cfg.remote)
    fd_read_timeout(sock.fileno(), 5)
    return sock.getsockopt(socket.SOL_SOCKET, socket.SO_INCOMING_CPU)


def test_rss_input_xfrm(cfg, ipver):
    """
    Test symmetric input_xfrm.
    If symmetric RSS hash is configured, send traffic twice, swapping the
    src/dst UDP ports, and verify that the same queue is receiving the traffic
    in both cases (IPs are constant).
    """

    if multiprocessing.cpu_count() < 2:
        raise KsftSkipEx("Need at least two CPUs to test symmetric RSS hash")

    cfg.require_cmd("socat", remote=True)

    if not hasattr(socket, "SO_INCOMING_CPU"):
        raise KsftSkipEx("socket.SO_INCOMING_CPU was added in Python 3.11")

    input_xfrm = cfg.ethnl.rss_get(
        {'header': {'dev-name': cfg.ifname}}).get('input-xfrm')

    # Check for symmetric xor/or-xor
    if not input_xfrm or (input_xfrm != 1 and input_xfrm != 2):
        raise KsftSkipEx("Symmetric RSS hash not requested")

    cpus = set()
    successful = 0
    for _ in range(100):
        try:
            port1 = rand_port(socket.SOCK_DGRAM)
            port2 = rand_port(socket.SOCK_DGRAM)
            cpu1 = traffic(cfg, port1, port2, ipver)
            cpu2 = traffic(cfg, port2, port1, ipver)
            cpus.update([cpu1, cpu2])
            ksft_eq(
                cpu1, cpu2, comment=f"Received traffic on different cpus with ports ({port1 = }, {port2 = }) while symmetric hash is configured")

            successful += 1
            if successful == 10:
                break
        except:
            continue
    else:
        raise KsftFailEx("Failed to run traffic")

    ksft_ge(len(cpus), 2,
            comment=f"Received traffic on less than two cpus {cpus = }")


def test_rss_input_xfrm_ipv4(cfg):
    cfg.require_ipver("4")
    test_rss_input_xfrm(cfg, "4")


def test_rss_input_xfrm_ipv6(cfg):
    cfg.require_ipver("6")
    test_rss_input_xfrm(cfg, "6")


def main() -> None:
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        cfg.ethnl = EthtoolFamily()
        cfg.netdevnl = NetdevFamily()

        ksft_run([test_rss_input_xfrm_ipv4, test_rss_input_xfrm_ipv6],
                 args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
