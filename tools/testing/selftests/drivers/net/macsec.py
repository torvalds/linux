#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""MACsec tests."""

import os

from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_raises
from lib.py import ksft_variants, KsftNamedVariant
from lib.py import CmdExitFailure, KsftSkipEx
from lib.py import NetDrvEpEnv
from lib.py import cmd, ip, defer, ethtool

MACSEC_KEY = "12345678901234567890123456789012"
MACSEC_VLAN_VID = 10

# Unique prefix per run to avoid collisions in the shared netns.
# Keep it short: IFNAMSIZ is 16 (incl. NUL), and VLAN names append ".<vid>".
MACSEC_PFX = f"ms{os.getpid()}_"


def _macsec_name(idx=0):
    return f"{MACSEC_PFX}{idx}"


def _get_macsec_offload(dev):
    """Returns macsec offload mode string from ip -d link show."""
    info = ip(f"-d link show dev {dev}", json=True)[0]
    return info.get("linkinfo", {}).get("info_data", {}).get("offload")


def _get_features(dev):
    """Returns ethtool features dict for a device."""
    return ethtool(f"-k {dev}", json=True)[0]


def _require_ip_macsec(cfg):
    """SKIP if iproute2 on local or remote lacks 'ip macsec' support."""
    for host in [None, cfg.remote]:
        out = cmd("ip macsec help", fail=False, host=host)
        if "Usage" not in out.stdout + out.stderr:
            where = "remote" if host else "local"
            raise KsftSkipEx(f"iproute2 too old on {where},"
                             " missing macsec support")


def _require_ip_macsec_offload():
    """SKIP if local iproute2 doesn't understand 'ip macsec offload'."""
    out = cmd("ip macsec help", fail=False)
    if "offload" not in out.stdout + out.stderr:
        raise KsftSkipEx("iproute2 too old, missing macsec offload")


def _require_macsec_offload(cfg):
    """SKIP if local device doesn't support macsec-hw-offload."""
    _require_ip_macsec_offload()
    try:
        feat = ethtool(f"-k {cfg.ifname}", json=True)[0]
    except (CmdExitFailure, IndexError) as e:
        raise KsftSkipEx(
            f"can't query features: {e}") from e
    if not feat.get("macsec-hw-offload", {}).get("active"):
        raise KsftSkipEx("macsec-hw-offload not supported")


def _get_mac(ifname, host=None):
    """Gets MAC address of an interface."""
    dev = ip(f"link show dev {ifname}", json=True, host=host)
    return dev[0]["address"]


def _setup_macsec_sa(cfg, name):
    """Adds matching TX/RX SAs on both ends."""
    local_mac = _get_mac(name)
    remote_mac = _get_mac(name, host=cfg.remote)

    ip(f"macsec add {name} tx sa 0 pn 1 on key 01 {MACSEC_KEY}")
    ip(f"macsec add {name} rx port 1 address {remote_mac}")
    ip(f"macsec add {name} rx port 1 address {remote_mac} "
       f"sa 0 pn 1 on key 02 {MACSEC_KEY}")

    ip(f"macsec add {name} tx sa 0 pn 1 on key 02 {MACSEC_KEY}",
       host=cfg.remote)
    ip(f"macsec add {name} rx port 1 address {local_mac}", host=cfg.remote)
    ip(f"macsec add {name} rx port 1 address {local_mac} "
       f"sa 0 pn 1 on key 01 {MACSEC_KEY}", host=cfg.remote)


def _setup_macsec_devs(cfg, name, offload):
    """Creates macsec devices on both ends.

    Only the local device gets HW offload; the remote always uses software
    MACsec since it may not support offload at all.
    """
    offload_arg = "mac" if offload else "off"

    ip(f"link add link {cfg.ifname} {name} "
       f"type macsec encrypt on offload {offload_arg}")
    defer(ip, f"link del {name}")
    ip(f"link add link {cfg.remote_ifname} {name} "
       f"type macsec encrypt on", host=cfg.remote)
    defer(ip, f"link del {name}", host=cfg.remote)


def _set_offload(name, offload):
    """Sets offload on the local macsec device only."""
    offload_arg = "mac" if offload else "off"

    ip(f"link set {name} type macsec encrypt on offload {offload_arg}")


def _setup_vlans(cfg, name, vid):
    """Adds VLANs on top of existing macsec devs."""
    vlan_name = f"{name}.{vid}"

    ip(f"link add link {name} {vlan_name} type vlan id {vid}")
    defer(ip, f"link del {vlan_name}")
    ip(f"link add link {name} {vlan_name} type vlan id {vid}", host=cfg.remote)
    defer(ip, f"link del {vlan_name}", host=cfg.remote)


def _setup_vlan_ips(cfg, name, vid):
    """Adds VLANs and IPs and brings up the macsec + VLAN devices."""
    local_ip = "198.51.100.1"
    remote_ip = "198.51.100.2"
    vlan_name = f"{name}.{vid}"

    ip(f"addr add {local_ip}/24 dev {vlan_name}")
    ip(f"addr add {remote_ip}/24 dev {vlan_name}", host=cfg.remote)
    ip(f"link set {name} up")
    ip(f"link set {name} up", host=cfg.remote)
    ip(f"link set {vlan_name} up")
    ip(f"link set {vlan_name} up", host=cfg.remote)

    return vlan_name, remote_ip


def test_offload_api(cfg) -> None:
    """MACsec offload API: create SecY, add SA/rx, toggle offload."""

    _require_macsec_offload(cfg)
    ms0 = _macsec_name(0)
    ms1 = _macsec_name(1)
    ms2 = _macsec_name(2)

    # Create 3 SecY with offload
    ip(f"link add link {cfg.ifname} {ms0} type macsec "
       f"port 4 encrypt on offload mac")
    defer(ip, f"link del {ms0}")

    ip(f"link add link {cfg.ifname} {ms1} type macsec "
       f"address aa:bb:cc:dd:ee:ff port 5 encrypt on offload mac")
    defer(ip, f"link del {ms1}")

    ip(f"link add link {cfg.ifname} {ms2} type macsec "
       f"sci abbacdde01020304 encrypt on offload mac")
    defer(ip, f"link del {ms2}")

    # Add TX SA
    ip(f"macsec add {ms0} tx sa 0 pn 1024 on "
       "key 01 12345678901234567890123456789012")

    # Add RX SC + SA
    ip(f"macsec add {ms0} rx port 1234 address 1c:ed:de:ad:be:ef")
    ip(f"macsec add {ms0} rx port 1234 address 1c:ed:de:ad:be:ef "
       "sa 0 pn 1 on key 00 0123456789abcdef0123456789abcdef")

    # Can't disable offload when SAs are configured
    with ksft_raises(CmdExitFailure):
        ip(f"link set {ms0} type macsec offload off")
    with ksft_raises(CmdExitFailure):
        ip(f"macsec offload {ms0} off")

    # Toggle offload via rtnetlink on SA-free device
    ip(f"link set {ms2} type macsec offload off")
    ip(f"link set {ms2} type macsec encrypt on offload mac")

    # Toggle offload via genetlink
    ip(f"macsec offload {ms2} off")
    ip(f"macsec offload {ms2} mac")


def test_max_secy(cfg) -> None:
    """nsim-only test for max number of SecYs."""

    cfg.require_nsim()
    _require_ip_macsec_offload()
    ms0 = _macsec_name(0)
    ms1 = _macsec_name(1)
    ms2 = _macsec_name(2)
    ms3 = _macsec_name(3)

    ip(f"link add link {cfg.ifname} {ms0} type macsec "
       f"port 4 encrypt on offload mac")
    defer(ip, f"link del {ms0}")

    ip(f"link add link {cfg.ifname} {ms1} type macsec "
       f"address aa:bb:cc:dd:ee:ff port 5 encrypt on offload mac")
    defer(ip, f"link del {ms1}")

    ip(f"link add link {cfg.ifname} {ms2} type macsec "
       f"sci abbacdde01020304 encrypt on offload mac")
    defer(ip, f"link del {ms2}")
    with ksft_raises(CmdExitFailure):
        ip(f"link add link {cfg.ifname} {ms3} "
           f"type macsec port 8 encrypt on offload mac")


def test_max_sc(cfg) -> None:
    """nsim-only test for max number of SCs."""

    cfg.require_nsim()
    _require_ip_macsec_offload()
    ms0 = _macsec_name(0)

    ip(f"link add link {cfg.ifname} {ms0} type macsec "
       f"port 4 encrypt on offload mac")
    defer(ip, f"link del {ms0}")
    ip(f"macsec add {ms0} rx port 1234 address 1c:ed:de:ad:be:ef")
    with ksft_raises(CmdExitFailure):
        ip(f"macsec add {ms0} rx port 1235 address 1c:ed:de:ad:be:ef")


def test_offload_state(cfg) -> None:
    """Offload state reflects configuration changes."""

    _require_macsec_offload(cfg)
    ms0 = _macsec_name(0)

    # Create with offload on
    ip(f"link add link {cfg.ifname} {ms0} type macsec "
       f"encrypt on offload mac")
    cleanup = defer(ip, f"link del {ms0}")

    ksft_eq(_get_macsec_offload(ms0), "mac",
            "created with offload: should be mac")
    feats_on_1 = _get_features(ms0)

    ip(f"link set {ms0} type macsec offload off")
    ksft_eq(_get_macsec_offload(ms0), "off",
            "offload disabled: should be off")
    feats_off_1 = _get_features(ms0)

    ip(f"link set {ms0} type macsec encrypt on offload mac")
    ksft_eq(_get_macsec_offload(ms0), "mac",
            "offload re-enabled: should be mac")
    ksft_eq(_get_features(ms0), feats_on_1,
            "features should match first offload-on snapshot")

    # Delete and recreate without offload
    cleanup.exec()
    ip(f"link add link {cfg.ifname} {ms0} type macsec")
    defer(ip, f"link del {ms0}")
    ksft_eq(_get_macsec_offload(ms0), "off",
            "created without offload: should be off")
    ksft_eq(_get_features(ms0), feats_off_1,
            "features should match first offload-off snapshot")

    ip(f"link set {ms0} type macsec encrypt on offload mac")
    ksft_eq(_get_macsec_offload(ms0), "mac",
            "offload enabled after create: should be mac")
    ksft_eq(_get_features(ms0), feats_on_1,
            "features should match first offload-on snapshot")


def _check_nsim_vid(cfg, vid, expected) -> None:
    """Checks if a VLAN is present. Only works on netdevsim."""

    nsim = cfg.get_local_nsim_dev()
    if not nsim:
        return

    vlan_path = os.path.join(nsim.nsims[0].dfs_dir, "vlan")
    with open(vlan_path, encoding="utf-8") as f:
        vids = f.read()
    found = f"ctag {vid}\n" in vids
    ksft_eq(found, expected,
            f"VLAN {vid} {'expected' if expected else 'not expected'}"
            f" in debugfs")


@ksft_variants([
    KsftNamedVariant("offloaded", True),
    KsftNamedVariant("software", False),
])
def test_vlan(cfg, offload) -> None:
    """Ping through VLAN-over-macsec."""

    _require_ip_macsec(cfg)
    if offload:
        _require_macsec_offload(cfg)
    else:
        _require_ip_macsec_offload()
    name = _macsec_name()
    _setup_macsec_devs(cfg, name, offload=offload)
    _setup_macsec_sa(cfg, name)
    _setup_vlans(cfg, name, MACSEC_VLAN_VID)
    vlan_name, remote_ip = _setup_vlan_ips(cfg, name, MACSEC_VLAN_VID)
    _check_nsim_vid(cfg, MACSEC_VLAN_VID, offload)
    # nsim doesn't handle the data path for offloaded macsec, so skip
    # the ping when offloaded on nsim.
    if not offload or not cfg.get_local_nsim_dev():
        cmd(f"ping -I {vlan_name} -c 1 -W 5 {remote_ip}")


@ksft_variants([
    KsftNamedVariant("on_to_off", True),
    KsftNamedVariant("off_to_on", False),
])
def test_vlan_toggle(cfg, offload) -> None:
    """Toggle offload: VLAN filters propagate/remove correctly."""

    _require_ip_macsec(cfg)
    _require_macsec_offload(cfg)
    name = _macsec_name()
    _setup_macsec_devs(cfg, name, offload=offload)
    _setup_vlans(cfg, name, MACSEC_VLAN_VID)
    _check_nsim_vid(cfg, MACSEC_VLAN_VID, offload)
    _set_offload(name, offload=not offload)
    _check_nsim_vid(cfg, MACSEC_VLAN_VID, not offload)
    vlan_name, remote_ip = _setup_vlan_ips(cfg, name, MACSEC_VLAN_VID)
    _setup_macsec_sa(cfg, name)
    # nsim doesn't handle the data path for offloaded macsec, so skip
    # the ping when the final state is offloaded on nsim.
    if offload or not cfg.get_local_nsim_dev():
        cmd(f"ping -I {vlan_name} -c 1 -W 5 {remote_ip}")


def main() -> None:
    """Main program."""
    with NetDrvEpEnv(__file__) as cfg:
        ksft_run([test_offload_api,
                  test_max_secy,
                  test_max_sc,
                  test_offload_state,
                  test_vlan,
                  test_vlan_toggle,
                  ], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
