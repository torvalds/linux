#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
GRO (Generic Receive Offload) conformance tests.

Validates that GRO coalescing works correctly by running the gro
binary in different configurations and checking for correct packet
coalescing behavior.

Test cases:
  - data_same: Same size data packets coalesce
  - data_lrg_sml: Large packet followed by smaller one coalesces
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
"""

import os
from lib.py import ksft_run, ksft_exit, ksft_pr
from lib.py import NetDrvEpEnv, KsftXfailEx
from lib.py import bkg, cmd, defer, ethtool, ip
from lib.py import ksft_variants


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


def _write_defer_restore(cfg, path, val, defer_undo=False):
    with open(path, "r", encoding="utf-8") as fp:
        orig_val = fp.read().strip()
        if str(val) == orig_val:
            return
    with open(path, "w", encoding="utf-8") as fp:
        fp.write(val)
    if defer_undo:
        defer(_write_defer_restore, cfg, path, orig_val)


def _set_mtu_restore(dev, mtu, host):
    if dev['mtu'] < mtu:
        ip(f"link set dev {dev['ifname']} mtu {mtu}", host=host)
        defer(ip, f"link set dev {dev['ifname']} mtu {dev['mtu']}", host=host)


def _set_ethtool_feat(dev, current, feats, host=None):
    s2n = {True: "on", False: "off"}

    new = ["-K", dev]
    old = ["-K", dev]
    no_change = True
    for name, state in feats.items():
        new += [name, s2n[state]]
        old += [name, s2n[current[name]["active"]]]

        if current[name]["active"] != state:
            no_change = False
            if current[name]["fixed"]:
                raise KsftXfailEx(f"Device does not support {name}")
    if no_change:
        return

    eth_cmd = ethtool(" ".join(new), host=host)
    defer(ethtool, " ".join(old), host=host)

    # If ethtool printed something kernel must have modified some features
    if eth_cmd.stdout:
        ksft_pr(eth_cmd)


def _setup(cfg, mode, test_name):
    """ Setup hardware loopback mode for GRO testing. """

    if not hasattr(cfg, "bin_remote"):
        cfg.bin_local = cfg.test_dir / "gro"
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

        _write_defer_restore(cfg, flush_path, "200000", defer_undo=True)
        _write_defer_restore(cfg, irq_path, "10", defer_undo=True)

        _set_ethtool_feat(cfg.ifname, cfg.feat,
                          {"generic-receive-offload": True,
                           "rx-gro-hw": False,
                           "large-receive-offload": False})
    elif mode == "hw":
        _set_ethtool_feat(cfg.ifname, cfg.feat,
                          {"generic-receive-offload": False,
                           "rx-gro-hw": True,
                           "large-receive-offload": False})

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

            _set_ethtool_feat(cfg.ifname, feat,
                              {"generic-receive-offload": True,
                               "rx-gro-hw": True,
                               "large-receive-offload": False})
    elif mode == "lro":
        # netdevsim advertises LRO for feature inheritance testing with
        # bonding/team tests but it doesn't actually perform the offload
        cfg.require_nsim(nsim_test=False)

        _set_ethtool_feat(cfg.ifname, cfg.feat,
                          {"generic-receive-offload": False,
                           "rx-gro-hw": False,
                           "large-receive-offload": True})

    try:
        # Disable TSO for local tests
        cfg.require_nsim()  # will raise KsftXfailEx if not running on nsim

        _set_ethtool_feat(cfg.remote_ifname, cfg.remote_feat,
                          {"tcp-segmentation-offload": False},
                          host=cfg.remote)
    except KsftXfailEx:
        pass


def _gro_variants():
    """Generator that yields all combinations of protocol and test types."""

    # Tests that work for all protocols
    common_tests = [
        "data_same", "data_lrg_sml", "data_sml_lrg",
        "ack",
        "flags_psh", "flags_syn", "flags_rst", "flags_urg", "flags_cwr",
        "tcp_csum", "tcp_seq", "tcp_ts", "tcp_opt",
        "ip_ecn", "ip_tos",
        "large_max", "large_rem",
    ]

    # Tests specific to IPv4
    ipv4_tests = [
        "ip_ttl", "ip_opt", "ip_frag4",
        "ip_id_df1_inc", "ip_id_df1_fixed",
        "ip_id_df0_inc", "ip_id_df0_fixed",
        "ip_id_df1_inc_fixed", "ip_id_df1_fixed_inc",
    ]

    # Tests specific to IPv6
    ipv6_tests = [
        "ip_frag6", "ip_v6ext_same", "ip_v6ext_diff",
    ]

    for mode in ["sw", "hw", "lro"]:
        for protocol in ["ipv4", "ipv6", "ipip"]:
            for test_name in common_tests:
                yield mode, protocol, test_name

            if protocol in ["ipv4", "ipip"]:
                for test_name in ipv4_tests:
                    yield mode, protocol, test_name
            elif protocol == "ipv6":
                for test_name in ipv6_tests:
                    yield mode, protocol, test_name


@ksft_variants(_gro_variants())
def test(cfg, mode, protocol, test_name):
    """Run a single GRO test with retries."""

    ipver = "6" if protocol[-1] == "6" else "4"
    cfg.require_ipver(ipver)

    _setup(cfg, mode, test_name)

    base_cmd_args = [
        f"--{protocol}",
        f"--dmac {_resolve_dmac(cfg, ipver)}",
        f"--smac {cfg.remote_dev['address']}",
        f"--daddr {cfg.addr_v[ipver]}",
        f"--saddr {cfg.remote_addr_v[ipver]}",
        f"--test {test_name}",
        "--verbose"
    ]
    base_args = " ".join(base_cmd_args)

    # Each test is run 6 times to deflake, because given the receive timing,
    # not all packets that should coalesce will be considered in the same flow
    # on every try.
    max_retries = 6
    for attempt in range(max_retries):
        rx_cmd = f"{cfg.bin_local} {base_args} --rx --iface {cfg.ifname}"
        tx_cmd = f"{cfg.bin_remote} {base_args} --iface {cfg.remote_ifname}"

        fail_now = attempt >= max_retries - 1

        with bkg(rx_cmd, ksft_ready=True, exit_wait=True,
                 fail=fail_now) as rx_proc:
            cmd(tx_cmd, host=cfg.remote)

        if rx_proc.ret == 0:
            return

        ksft_pr(rx_proc)

        if test_name.startswith("large_") and os.environ.get("KSFT_MACHINE_SLOW"):
            ksft_pr(f"Ignoring {protocol}/{test_name} failure due to slow environment")
            return

        ksft_pr(f"Attempt {attempt + 1}/{max_retries} failed, retrying...")


def main() -> None:
    """ Ksft boiler plate main """

    with NetDrvEpEnv(__file__) as cfg:
        ksft_run(cases=[test], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
