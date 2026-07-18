#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Traffic test for VXLAN + IPsec crypto-offload."""

import os

from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_ge
from lib.py import ksft_variants, KsftNamedVariant, KsftSkipEx
from lib.py import CmdExitFailure, NetDrvEpEnv, cmd, defer, ethtool, ip
from lib.py import Iperf3Runner

# Inner tunnel addresses - TEST-NET-2 (RFC 5737) / doc prefix (RFC 3849)
INNER_V4_LOCAL = "198.51.100.1"
INNER_V4_REMOTE = "198.51.100.2"
INNER_V6_LOCAL = "2001:db8:100::1"
INNER_V6_REMOTE = "2001:db8:100::2"

# ESP parameters
SPI_OUT = "0x1000"
SPI_IN = "0x1001"
# 128-bit key + 32-bit salt = 20 bytes hex, 128-bit ICV
ESP_AEAD = "aead 'rfc4106(gcm(aes))' 0x" + "01" * 20 + " 128"


def xfrm(args, host=None):
    """Runs 'ip xfrm' via shell to preserve parentheses in algo names."""
    cmd(f"ip xfrm {args}", shell=True, host=host)


def check_xfrm_offload_support():
    """Skips if iproute2 lacks xfrm offload support."""
    out = cmd("ip xfrm state help", fail=False)
    if "offload" not in out.stdout + out.stderr:
        raise KsftSkipEx("iproute2 too old, missing xfrm offload")


def check_esp_hw_offload(cfg):
    """Skips if device lacks esp-hw-offload support."""
    check_xfrm_offload_support()
    try:
        feat = ethtool(f"-k {cfg.ifname}", json=True)[0]
    except (CmdExitFailure, IndexError) as e:
        raise KsftSkipEx(f"can't query features: {e}") from e
    if not feat.get("esp-hw-offload", {}).get("active"):
        raise KsftSkipEx("Device does not support esp-hw-offload")


def get_tx_drops(cfg):
    """Returns TX dropped counter from the physical device."""
    stats = ip("-s -s link show dev " + cfg.ifname, json=True)[0]
    return stats["stats64"]["tx"]["dropped"]


def setup_vxlan_ipsec(cfg, outer_ipver, inner_ipver):
    """Sets up VXLAN tunnel with IPsec transport-mode crypto-offload."""
    vxlan_name = f"vx{os.getpid()}"
    local_addr = cfg.addr_v[outer_ipver]
    remote_addr = cfg.remote_addr_v[outer_ipver]

    if inner_ipver == "4":
        inner_local = f"{INNER_V4_LOCAL}/24"
        inner_remote = f"{INNER_V4_REMOTE}/24"
        addr_extra = ""
    else:
        inner_local = f"{INNER_V6_LOCAL}/64"
        inner_remote = f"{INNER_V6_REMOTE}/64"
        addr_extra = " nodad"

    if outer_ipver == "6":
        vxlan_opts = "udp6zerocsumtx udp6zerocsumrx"
    else:
        vxlan_opts = "noudpcsum"

    # VXLAN tunnel - local side
    ip(f"link add {vxlan_name} type vxlan id 100 dstport 4789 {vxlan_opts} "
       f"local {local_addr} remote {remote_addr} dev {cfg.ifname}")
    defer(ip, f"link del {vxlan_name}")
    ip(f"addr add {inner_local} dev {vxlan_name}{addr_extra}")
    ip(f"link set {vxlan_name} up")

    # VXLAN tunnel - remote side
    ip(f"link add {vxlan_name} type vxlan id 100 dstport 4789 {vxlan_opts} "
       f"local {remote_addr} remote {local_addr} dev {cfg.remote_ifname}",
       host=cfg.remote)
    defer(ip, f"link del {vxlan_name}", host=cfg.remote)
    ip(f"addr add {inner_remote} dev {vxlan_name}{addr_extra}",
       host=cfg.remote)
    ip(f"link set {vxlan_name} up", host=cfg.remote)

    # xfrm state - local outbound SA
    xfrm(f"state add src {local_addr} dst {remote_addr} "
         f"proto esp spi {SPI_OUT} "
         f"{ESP_AEAD} "
         f"mode transport offload crypto dev {cfg.ifname} dir out")
    defer(xfrm, f"state del src {local_addr} dst {remote_addr} "
                f"proto esp spi {SPI_OUT}")

    # xfrm state - local inbound SA
    xfrm(f"state add src {remote_addr} dst {local_addr} "
         f"proto esp spi {SPI_IN} "
         f"{ESP_AEAD} "
         f"mode transport offload crypto dev {cfg.ifname} dir in")
    defer(xfrm, f"state del src {remote_addr} dst {local_addr} "
                f"proto esp spi {SPI_IN}")

    # xfrm state - remote outbound SA (mirror, software crypto)
    xfrm(f"state add src {remote_addr} dst {local_addr} "
         f"proto esp spi {SPI_IN} "
         f"{ESP_AEAD} "
         f"mode transport",
         host=cfg.remote)
    defer(xfrm, f"state del src {remote_addr} dst {local_addr} "
                f"proto esp spi {SPI_IN}", host=cfg.remote)

    # xfrm state - remote inbound SA (mirror, software crypto)
    xfrm(f"state add src {local_addr} dst {remote_addr} "
         f"proto esp spi {SPI_OUT} "
         f"{ESP_AEAD} "
         f"mode transport",
         host=cfg.remote)
    defer(xfrm, f"state del src {local_addr} dst {remote_addr} "
                f"proto esp spi {SPI_OUT}", host=cfg.remote)

    # xfrm policy - local out
    xfrm(f"policy add src {local_addr} dst {remote_addr} "
         f"proto udp dport 4789 dir out "
         f"tmpl src {local_addr} dst {remote_addr} proto esp mode transport")
    defer(xfrm, f"policy del src {local_addr} dst {remote_addr} "
                f"proto udp dport 4789 dir out")

    # xfrm policy - local in
    xfrm(f"policy add src {remote_addr} dst {local_addr} "
         f"proto udp dport 4789 dir in "
         f"tmpl src {remote_addr} dst {local_addr} proto esp mode transport")
    defer(xfrm, f"policy del src {remote_addr} dst {local_addr} "
                f"proto udp dport 4789 dir in")

    # xfrm policy - remote out
    xfrm(f"policy add src {remote_addr} dst {local_addr} "
         f"proto udp dport 4789 dir out "
         f"tmpl src {remote_addr} dst {local_addr} proto esp mode transport",
         host=cfg.remote)
    defer(xfrm, f"policy del src {remote_addr} dst {local_addr} "
                f"proto udp dport 4789 dir out", host=cfg.remote)

    # xfrm policy - remote in
    xfrm(f"policy add src {local_addr} dst {remote_addr} "
         f"proto udp dport 4789 dir in "
         f"tmpl src {local_addr} dst {remote_addr} proto esp mode transport",
         host=cfg.remote)
    defer(xfrm, f"policy del src {local_addr} dst {remote_addr} "
                f"proto udp dport 4789 dir in", host=cfg.remote)


def _vxlan_ipsec_variants():
    """Generates outer/inner IP version variants."""
    for outer in ["4", "6"]:
        for inner in ["4", "6"]:
            yield KsftNamedVariant(f"outer_v{outer}_inner_v{inner}", outer, inner)


@ksft_variants(_vxlan_ipsec_variants())
def test_vxlan_ipsec_crypto_offload(cfg, outer_ipver, inner_ipver):
    """Tests VXLAN+IPsec crypto-offload has no TX drops."""
    cfg.require_ipver(outer_ipver)
    check_esp_hw_offload(cfg)

    setup_vxlan_ipsec(cfg, outer_ipver, inner_ipver)

    if inner_ipver == "4":
        inner_local = INNER_V4_LOCAL
        inner_remote = INNER_V4_REMOTE
        ping = "ping"
    else:
        inner_local = INNER_V6_LOCAL
        inner_remote = INNER_V6_REMOTE
        ping = "ping -6"

    cmd(f"{ping} -c 1 -W 2 {inner_remote}")

    drops_before = get_tx_drops(cfg)

    runner = Iperf3Runner(cfg, server_ip=inner_local,
                          client_ip=inner_remote)
    bw_gbps = runner.measure_bandwidth(reverse=True)

    cfg.wait_hw_stats_settle()
    drops_after = get_tx_drops(cfg)

    ksft_eq(drops_after - drops_before, 0,
            comment="TX drops during VXLAN+IPsec")
    ksft_ge(bw_gbps, 0.1,
            comment="Minimum 100Mbps over VXLAN+IPsec")


def main():
    """Runs VXLAN+IPsec crypto-offload GSO selftest."""
    with NetDrvEpEnv(__file__, nsim_test=False) as cfg:
        ksft_run([test_vxlan_ipsec_crypto_offload], args=(cfg,))
    ksft_exit()


if __name__ == "__main__":
    main()
