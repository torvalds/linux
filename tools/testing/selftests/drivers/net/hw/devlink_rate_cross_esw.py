#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""
Devlink Rate Cross-eswitch Scheduling Test Suite
==================================================

Control-plane tests for cross-eswitch TX scheduling via devlink-rate.
Validates that VFs from different PFs on the same chip can share
rate groups using the cross-device parent-dev attribute.

Preconditions:
- NETIF points to a bond device with exactly two interfaces.
- the interfaces must be two PFs from different devices sharing the same chip.
- (for mlx5): the two interfaces are in switchdev mode and configured in a LAG:
  - devlink dev eswitch set $DEV1 mode switchdev
  - devlink dev eswitch set $DEV2 mode switchdev
  - devlink dev param set $DEV1 name esw_multiport value 1 cmode runtime
  - devlink dev param set $DEV2 name esw_multiport value 1 cmode runtime
- test cases will be skipped if:
  - the number of interfaces in the bond device is != 2.
  - the kernel doesn't support devlink rates.
  - the devlink API doesn't support cross-device parents (ENODEV).
  - cross-esw rate scheduling returns EOPNOTSUPP.
"""

import errno
import glob
import os
import time

from lib.py import ksft_pr, ksft_eq, ksft_run, ksft_exit
from lib.py import KsftSkipEx, KsftFailEx
from lib.py import NetDrvEnv, DevlinkFamily
from lib.py import NlError
from lib.py import cmd, defer, ip, tool


# --- Discovery and setup ---


def get_bond_slaves(bond_ifname):
    """Returns sorted list of slave netdev names for a bond."""
    pattern = f"/sys/class/net/{bond_ifname}/lower_*"
    lowers = glob.glob(pattern)
    if not lowers:
        raise KsftSkipEx(f"No bond slaves for {bond_ifname}")
    slaves = []
    for path in sorted(lowers):
        name = os.path.basename(path)
        if name.startswith("lower_"):
            name = name[len("lower_"):]
        slaves.append(name)
    return slaves


def discover_pfs(cfg):
    """Discovers both PFs from bond slaves."""
    slaves = get_bond_slaves(cfg.ifname)
    if len(slaves) != 2:
        raise KsftSkipEx(f"Need 2 bond slaves, found {len(slaves)}")

    pf0, pf1 = slaves[0], slaves[1]
    ksft_pr(f"PF0: {pf0} PF1: {pf1}")
    return pf0, pf1


def get_pci_addr(ifname):
    """Resolves PCI address for a network interface."""
    return os.path.basename(os.path.realpath(f"/sys/class/net/{ifname}/device"))


def get_vf_port_index(pf_pci):
    """Finds devlink port-index for vf0 under pf_pci."""
    ports = tool("devlink", "port show", json=True)["port"]
    for port_name, props in ports.items():
        if port_name.startswith(f"pci/{pf_pci}/") and props.get("vfnum") == 0:
            return int(port_name.split("/")[-1])
    raise KsftSkipEx(f"VF port not found for {pf_pci}")


def cleanup_esw(pf):
    """Removes VFs if created by tests."""
    cmd(f"echo 0 > /sys/class/net/{pf}/device/sriov_numvfs", shell=True, fail=False)


def setup_esw(pf):
    """Creates 1 VF on 'pf'."""
    path = f"/sys/class/net/{pf}/device/sriov_numvfs"
    cmd(f"echo 0 > {path}", shell=True)
    cmd(f"echo 1 > {path}", shell=True)
    defer(cleanup_esw, pf)
    time.sleep(2)

    vf_dir = f"/sys/class/net/{pf}/device/virtfn0/net"
    entries = os.listdir(vf_dir) if os.path.isdir(vf_dir) else []
    if not entries:
        raise KsftSkipEx(f"VF not found for {pf}")
    ip(f"link set dev {entries[0]} up")

    pf_pci = get_pci_addr(pf)
    vf_idx = get_vf_port_index(pf_pci)
    ksft_pr(f"Created VF {vf_idx} on PF {pf} ({pf_pci})")
    return pf_pci, vf_idx


# --- Rate operation helpers ---


def rate_new(devnl, dev_pci, node_name, **kwargs):
    """Creates rate node."""
    params = {
        "bus-name": "pci",
        "dev-name": dev_pci,
        "rate-node-name": node_name,
    }
    params.update(kwargs)
    try:
        devnl.rate_new(params)
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("rate_new not supported") from e
        raise KsftFailEx("rate_new failed") from e


def rate_get(devnl, dev_pci, node_name):
    """Gets rate node."""
    params = {
        "bus-name": "pci",
        "dev-name": dev_pci,
        "rate-node-name": node_name,
    }
    return devnl.rate_get(params)


def rate_get_leaf(devnl, dev_pci, port_index):
    """Gets rate leaf (VF)."""
    params = {
        "bus-name": "pci",
        "dev-name": dev_pci,
        "port-index": port_index,
    }
    return devnl.rate_get(params)


def rate_del(devnl, dev_pci, node_name):
    """Deletes rate node."""
    devnl.rate_del({
        "bus-name": "pci",
        "dev-name": dev_pci,
        "rate-node-name": node_name,
    })


def rate_set_leaf(devnl, dev_pci, port_index, **kwargs):
    """Sets rate attributes on a leaf (VF)."""
    params = {
        "bus-name": "pci",
        "dev-name": dev_pci,
        "port-index": port_index,
    }
    params.update(kwargs)
    try:
        devnl.rate_set(params)
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("rate_set not supported") from e
        raise KsftFailEx("rate_set failed") from e


def rate_set_leaf_parent(devnl, dev_pci, port_index,
                         parent_name, parent_dev_pci=None):
    """Sets a leaf's parent, optionally cross-esw."""
    params = {
        "bus-name": "pci",
        "dev-name": dev_pci,
        "port-index": port_index,
        "rate-parent-node-name": parent_name,
    }
    if parent_dev_pci:
        params["parent-dev"] = {
            "bus-name": "pci",
            "dev-name": parent_dev_pci,
        }
    try:
        devnl.rate_set(params)
    except NlError as e:
        if e.error == errno.EOPNOTSUPP:
            raise KsftSkipEx("rate_set not supported") from e
        if parent_dev_pci and e.error == errno.ENODEV:
            raise KsftSkipEx("Cross-esw scheduling not supported") from e
        raise KsftFailEx("rate_set failed") from e


def rate_clear_leaf_parent(devnl, dev_pci, port_index):
    """Clears a leaf's parent."""
    rate_set_leaf_parent(devnl, dev_pci, port_index, "")


def rate_set_node(devnl, dev_pci, node_name, **kwargs):
    """Sets rate attributes on a node."""
    params = {
        "bus-name": "pci",
        "dev-name": dev_pci,
        "rate-node-name": node_name,
    }
    params.update(kwargs)
    devnl.rate_set(params)


# --- Test cases ---


def test_same_esw_parent(cfg):
    """Assigns PF0's VF to PF0's group (same esw baseline)."""
    pf0, _ = discover_pfs(cfg)
    pf0_pci, vf0_idx = setup_esw(pf0)

    rate_new(cfg.devnl, pf0_pci, "group0")
    defer(rate_del, cfg.devnl, pf0_pci, "group0")
    ksft_pr("rate-new succeeded")

    rate_set_leaf_parent(cfg.devnl, pf0_pci, vf0_idx, "group0")
    defer(rate_clear_leaf_parent, cfg.devnl, pf0_pci, vf0_idx)

    ksft_pr("Same-esw parent assignment succeeded")


def test_cross_esw_parent(cfg):
    """Sets cross-esw parent, then clear it."""
    pf0, pf1 = discover_pfs(cfg)
    pf0_pci, _ = setup_esw(pf0)
    pf1_pci, vf1_idx = setup_esw(pf1)

    rate_new(cfg.devnl, pf0_pci, "group1")
    defer(rate_del, cfg.devnl, pf0_pci, "group1")
    ksft_pr("rate-new succeeded")

    rate_set_leaf_parent(cfg.devnl, pf1_pci, vf1_idx,
                         "group1", parent_dev_pci=pf0_pci)
    defer(rate_clear_leaf_parent, cfg.devnl, pf1_pci, vf1_idx)

    ksft_pr("Cross-esw parent set and clear succeeded")


def test_tx_rates_on_cross_esw(cfg):
    """Sets tx_max on group and tx_share on leaves in a cross-esw setup."""
    pf0, pf1 = discover_pfs(cfg)
    pf0_pci, vf0_idx = setup_esw(pf0)
    pf1_pci, vf1_idx = setup_esw(pf1)

    rate_new(cfg.devnl, pf0_pci, "group2", **{"rate-tx-max": 10000000})
    defer(rate_del, cfg.devnl, pf0_pci, "group2")
    ksft_pr("rate-new succeeded")

    rate_set_leaf_parent(cfg.devnl, pf1_pci, vf1_idx,
                         "group2", parent_dev_pci=pf0_pci)
    defer(rate_clear_leaf_parent, cfg.devnl, pf1_pci, vf1_idx)
    ksft_pr("set parent cross-esw succeeded")

    rate_set_leaf_parent(cfg.devnl, pf0_pci, vf0_idx, "group2")
    defer(rate_clear_leaf_parent, cfg.devnl, pf0_pci, vf0_idx)
    ksft_pr("set parent same esw succeeded")

    rate_set_leaf(cfg.devnl, pf0_pci, vf0_idx, **{"rate-tx-share": 1000000})
    rate = rate_get_leaf(cfg.devnl, pf0_pci, vf0_idx)
    ksft_eq(rate["rate-tx-share"], 1000000)
    rate_set_leaf(cfg.devnl, pf1_pci, vf1_idx, **{"rate-tx-share": 2000000})
    rate = rate_get_leaf(cfg.devnl, pf1_pci, vf1_idx)
    ksft_eq(rate["rate-tx-share"], 2000000)
    rate_set_node(cfg.devnl, pf0_pci, "group2", **{"rate-tx-max": 250000000})
    rate = rate_get(cfg.devnl, pf0_pci, "group2")
    ksft_eq(rate["rate-tx-max"], 250000000)

    ksft_pr("tx_max and tx_share set on cross-esw group")


def main() -> None:
    """Main function."""

    with NetDrvEnv(__file__, nsim_test=False) as cfg:
        cfg.devnl = DevlinkFamily()

        ksft_run(
            cases=[
                test_same_esw_parent,
                test_cross_esw_parent,
                test_tx_rates_on_cross_esw,
            ],
            args=(cfg,),
        )
    ksft_exit()


if __name__ == "__main__":
    main()
