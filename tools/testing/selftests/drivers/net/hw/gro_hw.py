#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
HW GRO tests focusing on device machinery like stats, rather than protocol
processing.
"""

import glob
import re

from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import ksft_eq, ksft_ge, ksft_variants
from lib.py import NetDrvEpEnv, NetdevFamily
from lib.py import KsftSkipEx
from lib.py import bkg, cmd, defer, ethtool, ip


# gro.c uses hardcoded DPORT=8000
GRO_DPORT = 8000


def _get_queue_stats(cfg, queue_id):
    """Get stats for a specific Rx queue."""
    cfg.wait_hw_stats_settle()
    data = cfg.netnl.qstats_get({"ifindex": cfg.ifindex, "scope": ["queue"]},
                                dump=True)
    for q in data:
        if q.get('queue-type') == 'rx' and q.get('queue-id') == queue_id:
            return q
    return {}


def _resolve_dmac(cfg, ipver):
    """Find the destination MAC address for sending packets."""
    attr = "dmac" + ipver
    if hasattr(cfg, attr):
        return getattr(cfg, attr)

    route = ip(f"-{ipver} route get {cfg.addr_v[ipver]}",
               json=True, host=cfg.remote)[0]
    gw = route.get("gateway")
    if not gw:
        setattr(cfg, attr, cfg.dev['address'])
        return getattr(cfg, attr)

    cmd(f"ping -c1 -W0 -I{cfg.remote_ifname} {gw}", host=cfg.remote)
    neigh = ip(f"neigh get {gw} dev {cfg.remote_ifname}",
               json=True, host=cfg.remote)[0]
    setattr(cfg, attr, neigh['lladdr'])
    return getattr(cfg, attr)


def _require_ntuple(cfg):
    features = ethtool(f"-k {cfg.ifname}", json=True)[0]
    if not features["ntuple-filters"]["active"]:
        if features["ntuple-filters"]["fixed"]:
            raise KsftSkipEx("Device does not support ntuple-filters")
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
        raise KsftSkipEx(f"Need at least 2 queues, have {qcnt}")

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


def _run_gro_test(cfg, test_name, num_flows=None, ignore_fail=False,
                  order_check=False):
    """Run gro binary with given test and return output."""
    if not hasattr(cfg, "bin_remote"):
        cfg.bin_local = cfg.net_lib_dir / "gro"
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)

    ipver = cfg.addr_ipver
    protocol = f"--ipv{ipver}"
    dmac = _resolve_dmac(cfg, ipver)

    base_args = [
        protocol,
        f"--dmac {dmac}",
        f"--smac {cfg.remote_dev['address']}",
        f"--daddr {cfg.addr}",
        f"--saddr {cfg.remote_addr_v[ipver]}",
        f"--test {test_name}",
    ]
    if num_flows:
        base_args.append(f"--num-flows {num_flows}")
    if order_check:
        base_args.append("--order-check")

    args = " ".join(base_args)

    rx_cmd = f"{cfg.bin_local} {args} --rx --iface {cfg.ifname}"
    tx_cmd = f"{cfg.bin_remote} {args} --iface {cfg.remote_ifname}"

    with bkg(rx_cmd, ksft_ready=True, exit_wait=True, fail=False) as rx_proc:
        cmd(tx_cmd, host=cfg.remote)

    if not ignore_fail:
        ksft_eq(rx_proc.ret, 0)
        if rx_proc.ret != 0:
            ksft_pr(rx_proc)

    return rx_proc.stdout


def _require_hw_gro_stats(cfg, queue_id):
    """Check if device reports HW GRO stats for the queue."""
    stats = _get_queue_stats(cfg, queue_id)
    required = ['rx-packets', 'rx-hw-gro-packets', 'rx-hw-gro-wire-packets']
    for stat in required:
        if stat not in stats:
            raise KsftSkipEx(f"Driver does not report '{stat}' via qstats")


def _set_ethtool_feat(cfg, current, feats):
    """Set ethtool features with defer to restore original state."""
    s2n = {True: "on", False: "off"}

    new = ["-K", cfg.ifname]
    old = ["-K", cfg.ifname]
    no_change = True
    for name, state in feats.items():
        new += [name, s2n[state]]
        old += [name, s2n[current[name]["active"]]]

        if current[name]["active"] != state:
            no_change = False
            if current[name]["fixed"]:
                raise KsftSkipEx(f"Device does not support {name}")
    if no_change:
        return

    eth_cmd = ethtool(" ".join(new))
    defer(ethtool, " ".join(old))

    # If ethtool printed something kernel must have modified some features
    if eth_cmd.stdout:
        ksft_pr(eth_cmd)


def _setup_hw_gro(cfg):
    """Enable HW GRO on the device, disabling SW GRO."""
    feat = ethtool(f"-k {cfg.ifname}", json=True)[0]

    # Try to disable SW GRO and enable HW GRO
    _set_ethtool_feat(cfg, feat,
                      {"generic-receive-offload": False,
                       "rx-gro-hw": True,
                       "large-receive-offload": False})

    # Some NICs treat HW GRO as a GRO sub-feature so disabling GRO
    # will also clear HW GRO. Use a hack of installing XDP generic
    # to skip SW GRO, even when enabled.
    feat = ethtool(f"-k {cfg.ifname}", json=True)[0]
    if not feat["rx-gro-hw"]["active"]:
        ksft_pr("Driver clears HW GRO when SW GRO is cleared, using generic XDP workaround")
        prog = cfg.net_lib_dir / "xdp_dummy.bpf.o"
        ip(f"link set dev {cfg.ifname} xdpgeneric obj {prog} sec xdp")
        defer(ip, f"link set dev {cfg.ifname} xdpgeneric off")

        # Attaching XDP may change features, fetch the latest state
        feat = ethtool(f"-k {cfg.ifname}", json=True)[0]

        _set_ethtool_feat(cfg, feat,
                          {"generic-receive-offload": True,
                           "rx-gro-hw": True,
                           "large-receive-offload": False})


def _check_gro_stats(cfg, test_queue, stats_before,
                     expect_rx, expect_gro, expect_wire):
    """Validate GRO stats against expected values."""
    stats_after = _get_queue_stats(cfg, test_queue)

    rx_delta = (stats_after.get('rx-packets', 0) -
                stats_before.get('rx-packets', 0))
    gro_delta = (stats_after.get('rx-hw-gro-packets', 0) -
                 stats_before.get('rx-hw-gro-packets', 0))
    wire_delta = (stats_after.get('rx-hw-gro-wire-packets', 0) -
                  stats_before.get('rx-hw-gro-wire-packets', 0))

    ksft_eq(rx_delta, expect_rx, comment="rx-packets")
    ksft_eq(gro_delta, expect_gro, comment="rx-hw-gro-packets")
    ksft_eq(wire_delta, expect_wire, comment="rx-hw-gro-wire-packets")


def test_gro_stats_single(cfg):
    """
    Test that a single packet doesn't affect GRO stats.

    Send a single packet that cannot be coalesced (nothing to coalesce with).
    GRO stats should not increase since no coalescing occurred.
    rx-packets should increase by 2 (1 data + 1 FIN).
    """
    _setup_hw_gro(cfg)

    test_queue = _setup_isolated_queue(cfg)
    _require_hw_gro_stats(cfg, test_queue)

    stats_before = _get_queue_stats(cfg, test_queue)

    _run_gro_test(cfg, "single")

    # 1 data + 1 FIN = 2 rx-packets, no coalescing
    _check_gro_stats(cfg, test_queue, stats_before,
                     expect_rx=2, expect_gro=0, expect_wire=0)


def test_gro_stats_full(cfg):
    """
    Test GRO stats when overwhelming HW GRO capacity.

    Send 500 flows to exceed HW GRO flow capacity on a single queue.
    This should result in some packets not being coalesced.
    Validate that qstats match what gro.c observed.
    """
    _setup_hw_gro(cfg)

    test_queue = _setup_isolated_queue(cfg)
    _require_hw_gro_stats(cfg, test_queue)

    num_flows = 500
    stats_before = _get_queue_stats(cfg, test_queue)

    # Run capacity test - will likely fail because not all packets coalesce
    output = _run_gro_test(cfg, "capacity", num_flows=num_flows,
                           ignore_fail=True)

    # Parse gro.c output: "STATS: received=X wire=Y coalesced=Z"
    match = re.search(r'STATS: received=(\d+) wire=(\d+) coalesced=(\d+)',
                      output)
    if not match:
        raise KsftSkipEx(f"Could not parse gro.c output: {output}")

    rx_frames = int(match.group(2))
    gro_coalesced = int(match.group(3))

    ksft_ge(gro_coalesced, 1,
            comment="At least some packets should coalesce")

    # received + 1 FIN, coalesced super-packets, coalesced * 2 wire packets
    _check_gro_stats(cfg, test_queue, stats_before,
                     expect_rx=rx_frames + 1,
                     expect_gro=gro_coalesced,
                     expect_wire=gro_coalesced * 2)


@ksft_variants([4, 32, 512])
def test_gro_order(cfg, num_flows):
    """
    Test that HW GRO preserves packet ordering between flows.

    Packets may get delayed until the aggregate is released,
    but reordering between aggregates and packet terminating
    the aggregate and normal packets should not happen.

    Note that this test is stricter than truly required.
    Reordering packets between flows should not cause issues.
    This test will also fail if traffic is run over an ECMP fabric.
    """
    _setup_hw_gro(cfg)
    _setup_isolated_queue(cfg)

    _run_gro_test(cfg, "capacity", num_flows=num_flows, order_check=True)


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        cfg.netnl = NetdevFamily()
        ksft_run([test_gro_stats_single,
                  test_gro_stats_full,
                  test_gro_order], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
