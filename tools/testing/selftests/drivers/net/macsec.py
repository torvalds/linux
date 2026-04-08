#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""MACsec tests."""

import os

from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_raises
from lib.py import CmdExitFailure, KsftSkipEx
from lib.py import NetDrvEpEnv
from lib.py import cmd, ip, defer, ethtool

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


def main() -> None:
    """Main program."""
    with NetDrvEpEnv(__file__) as cfg:
        ksft_run([test_offload_api,
                  test_max_secy,
                  test_max_sc,
                  test_offload_state,
                  ], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
