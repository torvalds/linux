# SPDX-License-Identifier: GPL-2.0

"""
GRO (Generic Receive Offload) conformance tests.

Validates that GRO coalescing works correctly by running the gro
binary in different configurations and checking for correct packet
coalescing behavior.

The same test cases are run against all three coalescing implementations,
one mode per test program, because a full sweep takes too long to fit in
a single test's timeout:
  - gro_sw.py:  SW GRO (generic-receive-offload)
  - gro_hw.py:  HW GRO (rx-gro-hw)
  - gro_lro.py: LRO    (large-receive-offload)

Test cases:
  - data_same: Same size data packets coalesce
  - data_lrg_sml: Large packet followed by smaller one coalesces
  - data_lrg_1byte: Large packet followed by 1B one coalesces (Ethernet padding)
  - data_sml_lrg: Small packet followed by larger one doesn't coalesce
  - ack: Pure ACK packets do not coalesce
  - flags_psh: Packets with PSH flag don't coalesce
  - flags_syn: Packets with SYN flag don't coalesce
  - flags_rst: Packets with RST flag don't coalesce
  - flags_urg: Packets with URG flag don't coalesce
  - flags_cwr: Packets with CWR flag don't coalesce
  - tcp_csum: Packets with incorrect checksum don't coalesce
  - tcp_seq: Packets with non-consecutive seqno don't coalesce
  - tcp_ts: Packets with different timestamp options don't coalesce
  - tcp_opt: Packets with different TCP options don't coalesce
  - ip_ecn: Packets with different ECN don't coalesce
  - ip_tos: Packets with different TOS don't coalesce
  - ip_ttl: (IPv4) Packets with different TTL don't coalesce
  - ip_opt: (IPv4) Packets with IP options don't coalesce
  - ip_frag4: (IPv4) IPv4 fragments don't coalesce
  - ip_id_df*: (IPv4) IP ID field coalescing tests
  - ip_frag6: (IPv6) IPv6 fragments don't coalesce
  - ip_v6ext_same: (IPv6) IPv6 ext header with same payload coalesces
  - ip_v6ext_diff: (IPv6) IPv6 ext header with different payload doesn't coalesce
  - large_max: Packets exceeding GRO_MAX_SIZE don't coalesce
  - large_rem: Large packet remainder handling
  - big_tcp_data_same: Same size packets coalesce past IP_MAXPACKET
  - big_tcp_data_lrg_sml: Smaller last packet coalesces past IP_MAXPACKET
  - big_tcp_tcp_seq: Packets with 16-bit truncated seqno don't coalesce
  - big_tcp_large_max: Packets exceeding the BIG TCP limit don't coalesce
"""

import glob
import os
import re
from lib.py import ksft_run, ksft_exit, ksft_pr, ksft_eq
from lib.py import NetDrvEpEnv, KsftFailEx, KsftSkipEx, KsftXfailEx
from lib.py import NetdevFamily, EthtoolFamily
from lib.py import bkg, cmd, ctl_file_write, defer, ethtool, ip, set_ethtool_feat
from lib.py import ksft_variants, KsftNamedVariant


# gro.c uses hardcoded DPORT=8000
GRO_DPORT = 8000

BIG_TCP_GRO_MAX_SIZE = 128000


def _resolve_dmac(cfg, ipver):
    """
    Find the destination MAC address remote host should use to send packets
    towards the local host. It may be a router / gateway address.
    """

    attr = "dmac" + ipver
    # Cache the response across test cases
    if hasattr(cfg, attr):
        return getattr(cfg, attr)

    route = ip(f"-{ipver} route get {cfg.addr_v[ipver]}",
               json=True, host=cfg.remote)[0]
    gw = route.get("gateway")
    # Local L2 segment, address directly
    if not gw:
        setattr(cfg, attr, cfg.dev['address'])
        return getattr(cfg, attr)

    # ping to make sure neighbor is resolved,
    # bind to an interface, for v6 the GW is likely link local
    cmd(f"ping -c1 -W0 -I{cfg.remote_ifname} {gw}", host=cfg.remote)

    neigh = ip(f"neigh get {gw} dev {cfg.remote_ifname}",
               json=True, host=cfg.remote)[0]
    setattr(cfg, attr, neigh['lladdr'])
    return getattr(cfg, attr)


def _set_mtu_restore(dev, mtu, host):
    if dev['mtu'] < mtu:
        ip(f"link set dev {dev['ifname']} mtu {mtu}", host=host)
        defer(ip, f"link set dev {dev['ifname']} mtu {dev['mtu']}", host=host)


def _set_gro_size_restore(cfg, size):
    """
    Set the local device's GRO size limits, then confirm they stuck.
    """

    _set_mtu_restore(cfg.dev, 4096, None)
    _set_mtu_restore(cfg.remote_dev, 4096, cfg.remote)

    if "gro_max_size" not in cfg.dev or "gro_ipv4_max_size" not in cfg.dev:
        raise KsftSkipEx("iproute2 does not report the GRO size limits")

    if (cfg.dev["gro_max_size"] == size and
            cfg.dev["gro_ipv4_max_size"] == size):
        return

    old = (f"gro_max_size {cfg.dev['gro_max_size']} "
           f"gro_ipv4_max_size {cfg.dev['gro_ipv4_max_size']}")
    new = f"gro_max_size {size} gro_ipv4_max_size {size}"

    ip(f"link set dev {cfg.ifname} {new}")
    defer(ip, f"link set dev {cfg.ifname} {old}")

    dev = ip("-d link show dev " + cfg.ifname, json=True)[0]
    ksft_eq(dev["gro_max_size"], size, comment="gro_max_size not applied")
    ksft_eq(dev["gro_ipv4_max_size"], size,
            comment="gro_ipv4_max_size not applied")


def _get_queue_stats(cfg, queue_id):
    """Get stats for a specific Rx queue."""
    cfg.wait_hw_stats_settle()
    data = cfg.netnl.qstats_get({"ifindex": cfg.ifindex, "scope": ["queue"]},
                                dump=True)
    for q in data:
        if q.get('queue-type') == 'rx' and q.get('queue-id') == queue_id:
            return q
    return {}


def _require_ntuple(cfg):
    features = ethtool(f"-k {cfg.ifname}", json=True)[0]
    if not features["ntuple-filters"]["active"]:
        if features["ntuple-filters"]["fixed"]:
            raise KsftXfailEx("Device does not support ntuple-filters")
        ethtool(f"-K {cfg.ifname} ntuple-filters on")
        defer(ethtool, f"-K {cfg.ifname} ntuple-filters off")


def _setup_isolated_queue(cfg):
    """Set up an isolated queue for testing using ntuple filter.

    Remove queue 1 from the default RSS context and steer test traffic to it.
    """
    _require_ntuple(cfg)
    test_queue = 1

    qcnt = len(glob.glob(f"/sys/class/net/{cfg.ifname}/queues/rx-*"))
    if qcnt < 2:
        raise KsftXfailEx(f"Need at least 2 queues, have {qcnt}")

    # Remove queue 1 from default RSS context by setting its weight to 0
    weights = ["1"] * qcnt
    weights[test_queue] = "0"
    ethtool(f"-X {cfg.ifname} weight " + " ".join(weights))
    defer(ethtool, f"-X {cfg.ifname} default")

    # Set up ntuple filter to steer our test traffic to the isolated queue
    flow  = f"flow-type tcp{cfg.addr_ipver} "
    flow += f"dst-ip {cfg.addr} dst-port {GRO_DPORT} action {test_queue}"
    output = ethtool(f"-N {cfg.ifname} {flow}").stdout
    ntuple_id = int(output.split()[-1])
    defer(ethtool, f"-N {cfg.ifname} delete {ntuple_id}")

    return test_queue


def _setup_queue_count(cfg, num_queues):
    """Configure the NIC to use a specific number of queues."""
    channels = cfg.ethnl.channels_get({'header': {'dev-index': cfg.ifindex}})
    ch_max = channels.get('combined-max', 0)
    qcnt = channels['combined-count']

    if ch_max < num_queues:
        raise KsftXfailEx(f"Need at least {num_queues} queues, max={ch_max}")

    defer(ethtool, f"-L {cfg.ifname} combined {qcnt}")
    ethtool(f"-L {cfg.ifname} combined {num_queues}")


def _run_gro_bin(cfg, test_name, protocol=None, num_flows=None,
                 order_check=False, verbose=False, fail=False):
    """Run gro binary with given test and return the process result."""
    if not hasattr(cfg, "bin_remote"):
        cfg.bin_local = cfg.net_lib_dir / "gro"
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

    if protocol is None:
        ipver = cfg.addr_ipver
        protocol = f"ipv{ipver}"
    else:
        ipver = "6" if protocol[-1] == "6" else "4"

    dmac = _resolve_dmac(cfg, ipver)

    base_args = [
        f"--{protocol}",
        f"--dmac {dmac}",
        f"--smac {cfg.remote_dev['address']}",
        f"--daddr {cfg.addr_v[ipver]}",
        f"--saddr {cfg.remote_addr_v[ipver]}",
        f"--test {test_name}",
    ]
    if num_flows:
        base_args.append(f"--num-flows {num_flows}")
    if order_check:
        base_args.append("--order-check")
    if verbose:
        base_args.append("--verbose")

    args = " ".join(base_args)

    rx_cmd = f"{cfg.bin_local} {args} --rx --iface {cfg.ifname}"
    tx_cmd = f"{cfg.bin_remote} {args} --iface {cfg.remote_ifname}"

    with bkg(rx_cmd, ksft_ready=True, exit_wait=True, fail=fail) as rx_proc:
        cmd(tx_cmd, host=cfg.remote)

    return rx_proc


def _setup(cfg, mode, test_name):
    """ Setup hardware loopback mode for GRO testing. """

    if not hasattr(cfg, "bin_remote"):
        cfg.bin_local = cfg.net_lib_dir / "gro"
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

    if not hasattr(cfg, "feat"):
        cfg.feat = ethtool(f"-k {cfg.ifname}", json=True)[0]
        cfg.remote_feat = ethtool(f"-k {cfg.remote_ifname}",
                                  host=cfg.remote, json=True)[0]

    # "large_*" tests need at least 4k MTU
    if test_name.startswith("large_"):
        _set_mtu_restore(cfg.dev, 4096, None)
        _set_mtu_restore(cfg.remote_dev, 4096, cfg.remote)

    if mode == "sw":
        flush_path = f"/sys/class/net/{cfg.ifname}/gro_flush_timeout"
        irq_path = f"/sys/class/net/{cfg.ifname}/napi_defer_hard_irqs"

        # "big_tcp_*" tests need a longer timeout, use 2x the regular timeout
        if test_name.startswith("big_tcp_"):
            ctl_file_write(flush_path, "400000")
        else:
            ctl_file_write(flush_path, "200000")
        ctl_file_write(irq_path, "10")

        set_ethtool_feat(cfg.ifname, cfg.feat,
                         {"generic-receive-offload": True,
                          "rx-gro-hw": False,
                          "large-receive-offload": False})
    elif mode == "hw":
        set_ethtool_feat(cfg.ifname, cfg.feat,
                         {"generic-receive-offload": False,
                          "rx-gro-hw": True,
                          "large-receive-offload": False},
                          check=False)

        # Some NICs treat HW GRO as a GRO sub-feature so disabling GRO
        # will also clear HW GRO. Use a hack of installing XDP generic
        # to skip SW GRO, even when enabled.
        feat = ethtool(f"-k {cfg.ifname}", json=True)[0]
        if not feat["rx-gro-hw"]["active"]:
            ksft_pr("Driver clears HW GRO and SW GRO is cleared, using generic XDP workaround")
            prog = cfg.net_lib_dir / "xdp_dummy.bpf.o"
            ip(f"link set dev {cfg.ifname} xdpgeneric obj {prog} sec xdp")
            defer(ip, f"link set dev {cfg.ifname} xdpgeneric off")

            # Attaching XDP may change features, fetch the latest state
            feat = ethtool(f"-k {cfg.ifname}", json=True)[0]

            set_ethtool_feat(cfg.ifname, feat,
                             {"generic-receive-offload": True,
                              "rx-gro-hw": True,
                              "large-receive-offload": False})
    elif mode == "lro":
        # netdevsim advertises LRO for feature inheritance testing with
        # bonding/team tests but it doesn't actually perform the offload
        cfg.require_nsim(nsim_test=False)

        set_ethtool_feat(cfg.ifname, cfg.feat,
                         {"generic-receive-offload": False,
                          "rx-gro-hw": False,
                          "large-receive-offload": True})

    try:
        # Disable TSO for local tests
        cfg.require_nsim()  # will raise KsftXfailEx if not running on nsim

        set_ethtool_feat(cfg.remote_ifname, cfg.remote_feat,
                         {"tcp-segmentation-offload": False},
                         host=cfg.remote)
    except KsftXfailEx:
        pass

    if test_name.startswith("big_tcp_"):
        _set_gro_size_restore(cfg, BIG_TCP_GRO_MAX_SIZE)


def _gro_variants():
    """Generator that yields all combinations of protocol and test types."""

    # Tests that work for all protocols
    common_tests = [
        "data_same", "data_lrg_sml", "data_sml_lrg", "data_lrg_1byte",
        "data_burst",
        "ack",
        "flags_psh", "flags_syn", "flags_rst", "flags_urg", "flags_cwr",
        "tcp_csum", "tcp_seq", "tcp_ts", "tcp_opt",
        "ip_ecn", "ip_tos",
        "large_max", "large_rem",
    ]

    big_tcp_tests = [
        "big_tcp_data_same", "big_tcp_data_lrg_sml",
        "big_tcp_tcp_seq", "big_tcp_large_max",
    ]

    # Tests specific to IPv4
    ipv4_tests = [
        "ip_csum",
        "ip_ttl", "ip_opt", "ip_frag4",
        "ip_id_df1_inc", "ip_id_df1_fixed",
        "ip_id_df0_inc", "ip_id_df0_fixed",
        "ip_id_df1_inc_fixed", "ip_id_df1_fixed_inc",
    ]

    # Tests specific to IPv6
    ipv6_tests = [
        "ip_frag6", "ip_v6ext_same", "ip_v6ext_diff",
    ]

    for protocol in ["ipv4", "ipv6", "ipip", "ip6ip6"]:
        for test_name in common_tests:
            yield protocol, test_name

        if protocol in ["ipv4", "ipv6"]:
            for test_name in big_tcp_tests:
                yield protocol, test_name

        if protocol in ["ipv4", "ipip"]:
            for test_name in ipv4_tests:
                yield protocol, test_name
        elif protocol == "ipv6":
            for test_name in ipv6_tests:
                yield protocol, test_name


def run_test(cfg, mode, protocol, test_name):
    """Run a single GRO test with retries."""

    ipver = "6" if protocol[-1] == "6" else "4"
    cfg.require_ipver(ipver)

    _setup(cfg, mode, test_name)

    # Each test is run 6 times to deflake, because given the receive timing,
    # not all packets that should coalesce will be considered in the same flow
    # on every try.
    max_retries = 6
    for attempt in range(max_retries):
        fail_now = attempt >= max_retries - 1
        rx_proc = _run_gro_bin(cfg, test_name, protocol=protocol,
                               verbose=True, fail=fail_now)

        if rx_proc.ret == 0:
            return

        ksft_pr(rx_proc)

        # ret==42 means the receiver detected over-coalescing.
        # This is unambiguous proof of a bug, retries can only cause
        # false negatives.
        if rx_proc.ret == 42:
            raise KsftFailEx(f"GRO over-coalesced in {protocol}/{test_name}")

        if (test_name.startswith(("large_", "big_tcp_")) and
                os.environ.get("KSFT_MACHINE_SLOW")):
            ksft_pr(f"Ignoring {protocol}/{test_name} failure due to slow environment")
            return

        ksft_pr(f"Attempt {attempt + 1}/{max_retries} failed, retrying...")


@ksft_variants(_gro_variants())
def test(cfg, mode, protocol, test_name):
    """Run a single GRO test case."""
    run_test(cfg, mode, protocol, test_name)


def _capacity_variants():
    """Generate variants for capacity test: queue setup."""
    setups = [
        ("isolated", _setup_isolated_queue),
        ("1q", lambda cfg: _setup_queue_count(cfg, 1)),
        ("8q", lambda cfg: _setup_queue_count(cfg, 8)),
    ]
    for name, func in setups:
        yield KsftNamedVariant(name, func)


@ksft_variants(_capacity_variants())
def test_gro_capacity(cfg, mode, setup_func):
    """
    Probe GRO capacity.

    Start with 8 flows and increase by 2x on each successful run.
    Retry up to 3 times on failure.

    Queue setup variants:
      - isolated: Use a single queue isolated from RSS
      - 1q: Configure NIC to use 1 queue
      - 8q: Configure NIC to use 8 queues
    """
    max_retries = 3

    _setup(cfg, mode, "capacity")
    queue_id = setup_func(cfg)

    num_flows = 8
    while True:
        success = False
        for attempt in range(max_retries):
            if queue_id is not None:
                stats_before = _get_queue_stats(cfg, queue_id)

            rx_proc = _run_gro_bin(cfg, "capacity", num_flows=num_flows)
            output = rx_proc.stdout

            if queue_id is not None:
                stats_after = _get_queue_stats(cfg, queue_id)
                qstat_pkts = (stats_after.get('rx-packets', 0) -
                              stats_before.get('rx-packets', 0))
                gro_pkts = (stats_after.get('rx-hw-gro-packets', 0) -
                            stats_before.get('rx-hw-gro-packets', 0))
                qstat_str = f" qstat={qstat_pkts} hw-gro={gro_pkts}"
            else:
                qstat_str = ""

            # Parse and print STATS line
            match = re.search(
                r'STATS: received=(\d+) wire=(\d+) coalesced=(\d+)', output)
            if match:
                received = int(match.group(1))
                wire = int(match.group(2))
                coalesced = int(match.group(3))
                status = "PASS" if received == num_flows else "MISS"
                ksft_pr(f"flows={num_flows} attempt={attempt + 1} "
                        f"received={received} wire={wire} "
                        f"coalesced={coalesced}{qstat_str} [{status}]")
                if received == num_flows:
                    success = True
                    break
            else:
                ksft_pr(rx_proc)
                ksft_pr(f"flows={num_flows} attempt={attempt + 1}"
                        f"{qstat_str} [FAIL - can't parse stats]")

        if not success:
            ksft_pr(f"Stopped at {num_flows} flows")
            break

        num_flows *= 2


def gro_main(src_path, mode) -> None:
    """ Ksft boiler plate main, run all the cases in the given mode """

    with NetDrvEpEnv(src_path) as cfg:
        cfg.ethnl = EthtoolFamily()
        cfg.netnl = NetdevFamily()
        ksft_run(cases=[test, test_gro_capacity], args=(cfg, mode))
    ksft_exit()
