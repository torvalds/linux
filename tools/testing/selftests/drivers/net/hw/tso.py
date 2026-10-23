#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""A simple test for TSO."""

import fcntl
import mmap
import socket
import struct
import termios
import time

from lib.py import ksft_pr, ksft_run, ksft_exit, KsftSkipEx, KsftXfailEx
from lib.py import ksft_eq, ksft_ge, ksft_lt
from lib.py import EthtoolFamily, NetdevFamily, NetDrvEpEnv
from lib.py import bkg, cmd, defer, ethtool, ip, rand_port, wait_port_listen


MAP_HUGETLB = getattr(mmap, "MAP_HUGETLB", 0x40000)
MSG_ZEROCOPY = getattr(socket, "MSG_ZEROCOPY", 0x4000000)
SO_ZEROCOPY = getattr(socket, "SO_ZEROCOPY", 60)

GSO_LEGACY_MAX_SIZE = 65536

# Pool of the default hugepage size, the one /proc/meminfo reports on.
NR_HUGEPAGES = "/proc/sys/vm/nr_hugepages"


def default_huge_page_size():
    """Return the hugepage size in bytes"""
    try:
        with open("/proc/meminfo", encoding="utf-8") as meminfo:
            for line in meminfo:
                if line.startswith("Hugepagesize:"):
                    return int(line.split()[1]) * 1024
    except OSError:
        pass

    return 2 * 1024 * 1024


def hugepages_free():
    """Return the number of unused hugepages of the default size."""
    try:
        with open("/proc/meminfo", encoding="utf-8") as meminfo:
            for line in meminfo:
                if line.startswith("HugePages_Free:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def set_nr_hugepages(count):
    with open(NR_HUGEPAGES, "w", encoding="utf-8") as sysctl:
        sysctl.write(f"{count}\n")


def tx_dropped(ifname):
    with open(f"/sys/class/net/{ifname}/statistics/tx_dropped",
              encoding="utf-8") as counter:
        return int(counter.read())


def setup_hugepage():
    """Reserve one hugepage, and put the pool back afterwards."""
    if hugepages_free() >= 1:
        return

    try:
        with open(NR_HUGEPAGES, encoding="utf-8") as sysctl:
            old_count = int(sysctl.read())
        set_nr_hugepages(old_count + 1)
    except OSError as error:
        raise KsftSkipEx(f"Unable to reserve a hugepage: {error}") from error

    defer(set_nr_hugepages, old_count)

    if hugepages_free() < 1:
        raise KsftSkipEx("Unable to reserve a hugepage")


def mmap_large_buffer():
    """Allocate a buffer backed by one huge page."""
    size = default_huge_page_size()

    setup_hugepage()

    try:
        return mmap.mmap(-1, size,
                         flags=mmap.MAP_PRIVATE |
                               mmap.MAP_ANONYMOUS |
                               MAP_HUGETLB,
                         prot=mmap.PROT_READ)
    except OSError as e:
        raise KsftSkipEx(f"Unable to allocate a {size >> 20}MB hugepage "
                         f"buffer: {e}") from e


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


def setup_big_tcp(cfg):
    """Lift the GSO ceiling to what the device advertises for TSO."""
    if cfg.dev["tso_max_size"] <= GSO_LEGACY_MAX_SIZE:
        raise KsftSkipEx("Device does not support BIG TCP")

    ip(f"link set dev {cfg.ifname} "
       f"gso_max_size {cfg.dev['tso_max_size']} "
       f"gso_ipv4_max_size {cfg.dev['tso_max_size']}")

    defer(ip, f"link set dev {cfg.ifname} "
              f"gso_max_size {cfg.dev['gso_max_size']} "
              f"gso_ipv4_max_size {cfg.dev['gso_ipv4_max_size']}")


def sock_send_zerocopy(sock):
    """Send with MSG_ZEROCOPY, return the bytes queued."""
    try:
        sock.setsockopt(socket.SOL_SOCKET, SO_ZEROCOPY, 1)
    except OSError as e:
        raise KsftSkipEx(f"SO_ZEROCOPY not supported: {e}") from e

    with mmap_large_buffer() as tx_buf:
        sock.sendall(tx_buf, MSG_ZEROCOPY)
        return len(tx_buf)


def run_one_stream(cfg, ipver, remote_v4, remote_v6, should_lso):
    cfg.require_cmd("socat", local=False, remote=True)

    # Set recv window clamp to avoid overwhelming receiver on debug kernels
    # the 200k clamp should still let use reach > 15Gbps on real HW
    port = rand_port()
    listen_opts = f"{port},reuseport,tcp-window-clamp=200000"
    listen_cmd = f"socat -{ipver} -t 2 -u TCP-LISTEN:{listen_opts} /dev/null,ignoreeof"

    with bkg(listen_cmd, host=cfg.remote, exit_wait=True) as nc:
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

        # Check that at least 90% of the data was sent as LSO packets.
        # System noise may cause false negatives. Also header overheads
        # will add up to 5% of extra packes... The check is best effort.
        total_lso_wire  = len(buf) * 0.90 // cfg.dev["mtu"]
        total_lso_super = len(buf) * 0.90 // cfg.dev["tso_max_size"]

        # Make sure we have order of magnitude more LSO packets than
        # retransmits, in case TCP retransmitted all the LSO packets.
        ksft_lt(tcp_sock_get_retrans(sock), total_lso_wire / 16)
        sock.close()

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


def run_big_tcp_stream(cfg, ipver, remote_v4, remote_v6):
    """Send with MSG_ZEROCOPY out of a huge page, so the frags exceed 64kB."""
    cfg.require_cmd("socat", local=False, remote=True)

    # No clamping, as it would keep the frags under 64kB
    port = rand_port()
    listen_opts = f"{port},reuseport"
    listen_cmd = f"socat -{ipver} -t 2 -u TCP-LISTEN:{listen_opts} /dev/null,ignoreeof"

    with bkg(listen_cmd, host=cfg.remote, exit_wait=True):
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

        retrans_old = tcp_sock_get_retrans(sock)
        drops_old = tx_dropped(cfg.ifname)

        sent = sock_send_zerocopy(sock)
        sock_wait_drain(sock)

        drops = tx_dropped(cfg.ifname) - drops_old
        retrans = tcp_sock_get_retrans(sock) - retrans_old
        sock.close()

        ksft_eq(drops, 0, comment="Driver TX drops during BIG TCP send")

        # Same best effort bound as the plain stream.
        total_lso_wire = sent * 0.90 // cfg.dev["mtu"]
        ksft_lt(retrans, total_lso_wire / 16)


def build_tunnel(cfg, outer_ipver, tun_info):
    local_v4  = NetDrvEpEnv.nsim_v4_pfx + "1"
    local_v6  = NetDrvEpEnv.nsim_v6_pfx + "1"
    remote_v4 = NetDrvEpEnv.nsim_v4_pfx + "2"
    remote_v6 = NetDrvEpEnv.nsim_v6_pfx + "2"

    local_addr  = cfg.addr_v[outer_ipver]
    remote_addr = cfg.remote_addr_v[outer_ipver]

    tun_type = tun_info[0]
    tun_arg  = tun_info[1]
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


def restore_wanted_features(cfg):
    features_cmd = ""
    for feature in cfg.hw_features:
        setting = "on" if feature in cfg.wanted_features else "off"
        features_cmd += f" {feature} {setting}"
    try:
        ethtool(f"-K {cfg.ifname} {features_cmd}")
    except Exception as e:
        ksft_pr(f"WARNING: failure restoring wanted features: {e}")


def test_builder(name, cfg, outer_ipver, feature, tun=None, inner_ipver=None):
    """Construct specific tests from the common template."""
    def f(cfg):
        cfg.require_ipver(outer_ipver)
        defer(restore_wanted_features, cfg)

        if not cfg.have_stat_super_count and \
           not cfg.have_stat_wire_count:
            raise KsftSkipEx(f"Device does not support LSO queue stats")

        if feature not in cfg.hw_features:
            raise KsftSkipEx(f"Device does not support {feature}")

        # Run non-tunnel test cases under the BIG TCP limits too.
        big_tcp = "big_tcp" in name
        if big_tcp:
            setup_big_tcp(cfg)

        ipver = outer_ipver
        if tun:
            remote_v4, remote_v6 = build_tunnel(cfg, ipver, tun)
            ipver = inner_ipver
        else:
            remote_v4 = cfg.remote_addr_v["4"]
            remote_v6 = cfg.remote_addr_v["6"]

        # First test without the feature enabled.
        ethtool(f"-K {cfg.ifname} {feature} off")
        run_one_stream(cfg, ipver, remote_v4, remote_v6, should_lso=False)

        if big_tcp:
            run_big_tcp_stream(cfg, ipver, remote_v4, remote_v6)

        ethtool(f"-K {cfg.ifname} tx-gso-partial off")
        ethtool(f"-K {cfg.ifname} tx-tcp-mangleid-segmentation off")
        if feature in cfg.partial_features:
            ethtool(f"-K {cfg.ifname} tx-gso-partial on")
            if ipver == "4":
                ksft_pr("Testing with mangleid enabled")
                ethtool(f"-K {cfg.ifname} tx-tcp-mangleid-segmentation on")

        # Full feature enabled.
        ethtool(f"-K {cfg.ifname} {feature} on")
        run_one_stream(cfg, ipver, remote_v4, remote_v6, should_lso=True)

        if big_tcp:
            run_big_tcp_stream(cfg, ipver, remote_v4, remote_v6)

    f.__name__ = name + ((outer_ipver + "_") if tun else "") + "ipv" + inner_ipver
    return f


def query_nic_features(cfg) -> None:
    """Query and cache the NIC features."""
    cfg.have_stat_super_count = False
    cfg.have_stat_wire_count = False

    features = cfg.ethnl.features_get({"header": {"dev-index": cfg.ifindex}})

    cfg.wanted_features = set()
    for f in features["wanted"]["bits"]["bit"]:
        cfg.wanted_features.add(f["name"])

    cfg.hw_features = set()
    for f in features["hw"]["bits"]["bit"]:
        if f.get("value", False):
            cfg.hw_features.add(f["name"])

    # Check which features are supported via GSO partial
    cfg.partial_features = set()
    if 'tx-gso-partial' in cfg.hw_features:
        seg_features = {f for f in cfg.hw_features if "segmentation" in f}
        ethtool(f"-K {cfg.ifname} " +
                " ".join(f"{f} on" for f in seg_features))

        ethtool(f"-K {cfg.ifname} tx-gso-partial off")

        no_partial = set()
        features = cfg.ethnl.features_get({"header": {"dev-index": cfg.ifindex}})
        for f in features["active"]["bits"]["bit"]:
            no_partial.add(f["name"])
        cfg.partial_features = seg_features - no_partial
        ethtool(f"-K {cfg.ifname} tx-gso-partial on")

    restore_wanted_features(cfg)

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
            # name,       v4/v6  ethtool_feature               tun:(type, args, inner ip versions)
            ("",           "4", "tx-tcp-segmentation",         None),
            ("",           "6", "tx-tcp6-segmentation",        None),
            ("big_tcp_",   "4", "tx-tcp-segmentation",         None),
            ("big_tcp_",   "6", "tx-tcp6-segmentation",        None),
            ("vxlan",      "4", "tx-udp_tnl-segmentation",     ("vxlan", "id 100 dstport 4789 noudpcsum", ("4", "6"))),
            ("vxlan",      "6", "tx-udp_tnl-segmentation",     ("vxlan", "id 100 dstport 4789 udp6zerocsumtx udp6zerocsumrx", ("4", "6"))),
            ("vxlan_csum", "", "tx-udp_tnl-csum-segmentation", ("vxlan", "id 100 dstport 4789 udpcsum", ("4", "6"))),
            ("gre",        "4", "tx-gre-segmentation",         ("gre",   "", ("4", "6"))),
            ("gre",        "6", "tx-gre-segmentation",         ("ip6gre","", ("4", "6"))),
            ("ip",         "6", "tx-ipxip6-segmentation",      ("ip6tnl","mode any", ("4", "6"))),
            ("ip",         "4", "tx-ipxip4-segmentation",      ("sit","", ("6", ))),
            ("ip",         "4", "tx-ipxip4-segmentation",      ("ipip","", ("4", ))),
        )

        cases = []
        for outer_ipver in ["4", "6"]:
            for info in test_info:
                # Skip if test which only works for a specific IP version
                if info[1] and outer_ipver != info[1]:
                    continue

                if info[3]:
                    cases += [
                        test_builder(info[0], cfg, outer_ipver, info[2], info[3], inner_ipver)
                        for inner_ipver in info[3][2]
                    ]
                else:
                    cases.append(test_builder(info[0], cfg, outer_ipver, info[2], None, outer_ipver))

        ksft_run(cases=cases, args=(cfg, ))
    ksft_exit()


if __name__ == "__main__":
    main()
