#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Run the tools/testing/selftests/net/csum testsuite."""

import fcntl
import socket
import struct
import termios
import time

from lib.py import ksft_pr, ksft_run, ksft_exit, KsftSkipEx, KsftXfailEx
from lib.py import ksft_eq, ksft_ge, ksft_lt
from lib.py import EthtoolFamily, NetdevFamily, NetDrvEpEnv
from lib.py import bkg, cmd, defer, ethtool, ip, rand_port, wait_port_listen


def sock_wait_drain(sock, max_wait=1000):
    """Wait for all pending write data on the socket to get ACKed."""
    for _ in range(max_wait):
        one = b'\0' * 4
        outq = fcntl.ioctl(sock.fileno(), termios.TIOCOUTQ, one)
        outq = struct.unpack("I", outq)[0]
        if outq == 0:
            break
        time.sleep(0.01)
    ksft_eq(outq, 0)


def tcp_sock_get_retrans(sock):
    """Get the number of retransmissions for the TCP socket."""
    info = sock.getsockopt(socket.SOL_TCP, socket.TCP_INFO, 512)
    return struct.unpack("I", info[100:104])[0]


def run_one_stream(cfg, ipver, remote_v4, remote_v6, should_lso):
    cfg.require_cmd("socat", remote=True)

    port = rand_port()
    listen_cmd = f"socat -{ipver} -t 2 -u TCP-LISTEN:{port},reuseport /dev/null,ignoreeof"

    with bkg(listen_cmd, host=cfg.remote) as nc:
        wait_port_listen(port, host=cfg.remote)

        if ipver == "4":
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((remote_v4, port))
        else:
            sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
            sock.connect((remote_v6, port))

        # Small send to make sure the connection is working.
        sock.send("ping".encode())
        sock_wait_drain(sock)

        # Send 4MB of data, record the LSO packet count.
        qstat_old = cfg.netnl.qstats_get({"ifindex": cfg.ifindex}, dump=True)[0]
        buf = b"0" * 1024 * 1024 * 4
        sock.send(buf)
        sock_wait_drain(sock)
        qstat_new = cfg.netnl.qstats_get({"ifindex": cfg.ifindex}, dump=True)[0]

        # No math behind the 10 here, but try to catch cases where
        # TCP falls back to non-LSO.
        ksft_lt(tcp_sock_get_retrans(sock), 10)
        sock.close()

        # Check that at least 90% of the data was sent as LSO packets.
        # System noise may cause false negatives. Also header overheads
        # will add up to 5% of extra packes... The check is best effort.
        total_lso_wire  = len(buf) * 0.90 // cfg.dev["mtu"]
        total_lso_super = len(buf) * 0.90 // cfg.dev["tso_max_size"]
        if should_lso:
            if cfg.have_stat_super_count:
                ksft_ge(qstat_new['tx-hw-gso-packets'] -
                        qstat_old['tx-hw-gso-packets'],
                        total_lso_super,
                        comment="Number of LSO super-packets with LSO enabled")
            if cfg.have_stat_wire_count:
                ksft_ge(qstat_new['tx-hw-gso-wire-packets'] -
                        qstat_old['tx-hw-gso-wire-packets'],
                        total_lso_wire,
                        comment="Number of LSO wire-packets with LSO enabled")
        else:
            if cfg.have_stat_super_count:
                ksft_lt(qstat_new['tx-hw-gso-packets'] -
                        qstat_old['tx-hw-gso-packets'],
                        15, comment="Number of LSO super-packets with LSO disabled")
            if cfg.have_stat_wire_count:
                ksft_lt(qstat_new['tx-hw-gso-wire-packets'] -
                        qstat_old['tx-hw-gso-wire-packets'],
                        500, comment="Number of LSO wire-packets with LSO disabled")


def build_tunnel(cfg, outer_ipver, tun_info):
    local_v4  = NetDrvEpEnv.nsim_v4_pfx + "1"
    local_v6  = NetDrvEpEnv.nsim_v6_pfx + "1"
    remote_v4 = NetDrvEpEnv.nsim_v4_pfx + "2"
    remote_v6 = NetDrvEpEnv.nsim_v6_pfx + "2"

    local_addr  = cfg.addr_v[outer_ipver]
    remote_addr = cfg.remote_addr_v[outer_ipver]

    tun_type = tun_info[0]
    tun_arg  = tun_info[2]
    ip(f"link add {tun_type}-ksft type {tun_type} {tun_arg} local {local_addr} remote {remote_addr} dev {cfg.ifname}")
    defer(ip, f"link del {tun_type}-ksft")
    ip(f"link set dev {tun_type}-ksft up")
    ip(f"addr add {local_v4}/24 dev {tun_type}-ksft")
    ip(f"addr add {local_v6}/64 dev {tun_type}-ksft")

    ip(f"link add {tun_type}-ksft type {tun_type} {tun_arg} local {remote_addr} remote {local_addr} dev {cfg.remote_ifname}",
        host=cfg.remote)
    defer(ip, f"link del {tun_type}-ksft", host=cfg.remote)
    ip(f"link set dev {tun_type}-ksft up", host=cfg.remote)
    ip(f"addr add {remote_v4}/24 dev {tun_type}-ksft", host=cfg.remote)
    ip(f"addr add {remote_v6}/64 dev {tun_type}-ksft", host=cfg.remote)

    return remote_v4, remote_v6


def test_builder(name, cfg, outer_ipver, feature, tun=None, inner_ipver=None):
    """Construct specific tests from the common template."""
    def f(cfg):
        cfg.require_ipver(outer_ipver)

        if not cfg.have_stat_super_count and \
           not cfg.have_stat_wire_count:
            raise KsftSkipEx(f"Device does not support LSO queue stats")

        ipver = outer_ipver
        if tun:
            remote_v4, remote_v6 = build_tunnel(cfg, ipver, tun)
            ipver = inner_ipver
        else:
            remote_v4 = cfg.remote_addr_v["4"]
            remote_v6 = cfg.remote_addr_v["6"]

        tun_partial = tun and tun[1]
        # Tunnel which can silently fall back to gso-partial
        has_gso_partial = tun and 'tx-gso-partial' in cfg.features

        # For TSO4 via partial we need mangleid
        if ipver == "4" and feature in cfg.partial_features:
            ksft_pr("Testing with mangleid enabled")
            if 'tx-tcp-mangleid-segmentation' not in cfg.features:
                ethtool(f"-K {cfg.ifname} tx-tcp-mangleid-segmentation on")
                defer(ethtool, f"-K {cfg.ifname} tx-tcp-mangleid-segmentation off")

        # First test without the feature enabled.
        ethtool(f"-K {cfg.ifname} {feature} off")
        if has_gso_partial:
            ethtool(f"-K {cfg.ifname} tx-gso-partial off")
        run_one_stream(cfg, ipver, remote_v4, remote_v6, should_lso=False)

        # Now test with the feature enabled.
        # For compatible tunnels only - just GSO partial, not specific feature.
        if has_gso_partial:
            ethtool(f"-K {cfg.ifname} tx-gso-partial on")
            run_one_stream(cfg, ipver, remote_v4, remote_v6,
                           should_lso=tun_partial)

        # Full feature enabled.
        if feature in cfg.features:
            ethtool(f"-K {cfg.ifname} {feature} on")
            run_one_stream(cfg, ipver, remote_v4, remote_v6, should_lso=True)
        else:
            raise KsftXfailEx(f"Device does not support {feature}")

    f.__name__ = name + ((outer_ipver + "_") if tun else "") + "ipv" + inner_ipver
    return f


def query_nic_features(cfg) -> None:
    """Query and cache the NIC features."""
    cfg.have_stat_super_count = False
    cfg.have_stat_wire_count = False

    cfg.features = set()
    features = cfg.ethnl.features_get({"header": {"dev-index": cfg.ifindex}})
    for f in features["active"]["bits"]["bit"]:
        cfg.features.add(f["name"])

    # Check which features are supported via GSO partial
    cfg.partial_features = set()
    if 'tx-gso-partial' in cfg.features:
        ethtool(f"-K {cfg.ifname} tx-gso-partial off")

        no_partial = set()
        features = cfg.ethnl.features_get({"header": {"dev-index": cfg.ifindex}})
        for f in features["active"]["bits"]["bit"]:
            no_partial.add(f["name"])
        cfg.partial_features = cfg.features - no_partial
        ethtool(f"-K {cfg.ifname} tx-gso-partial on")

    stats = cfg.netnl.qstats_get({"ifindex": cfg.ifindex}, dump=True)
    if stats:
        if 'tx-hw-gso-packets' in stats[0]:
            ksft_pr("Detected qstat for LSO super-packets")
            cfg.have_stat_super_count = True
        if 'tx-hw-gso-wire-packets' in stats[0]:
            ksft_pr("Detected qstat for LSO wire-packets")
            cfg.have_stat_wire_count = True


def main() -> None:
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        cfg.ethnl = EthtoolFamily()
        cfg.netnl = NetdevFamily()

        query_nic_features(cfg)

        test_info = (
            # name,       v4/v6  ethtool_feature              tun:(type,    partial, args)
            ("",            "4", "tx-tcp-segmentation",           None),
            ("",            "6", "tx-tcp6-segmentation",          None),
            ("vxlan",        "", "tx-udp_tnl-segmentation",       ("vxlan",  True,  "id 100 dstport 4789 noudpcsum")),
            ("vxlan_csum",   "", "tx-udp_tnl-csum-segmentation",  ("vxlan",  False, "id 100 dstport 4789 udpcsum")),
            ("gre",         "4", "tx-gre-segmentation",           ("ipgre",  False,  "")),
            ("gre",         "6", "tx-gre-segmentation",           ("ip6gre", False,  "")),
        )

        cases = []
        for outer_ipver in ["4", "6"]:
            for info in test_info:
                # Skip if test which only works for a specific IP version
                if info[1] and outer_ipver != info[1]:
                    continue

                cases.append(test_builder(info[0], cfg, outer_ipver, info[2],
                                          tun=info[3], inner_ipver="4"))
                if info[3]:
                    cases.append(test_builder(info[0], cfg, outer_ipver, info[2],
                                              tun=info[3], inner_ipver="6"))

        ksft_run(cases=cases, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
