# SPDX-License-Identifier: GPL-2.0

"""
Netdev feature helper utilities for kernel selftests.

Provides common operations for changing device features via ethtool.
"""

from . import KsftXfailEx, defer, ethtool, ksft_pr


def set_ethtool_feat(dev, current, feats, host=None, check=True):
    """Set ethtool features with defer to restore original state."""
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

    if not check:
        return

    current = ethtool(f"-k {dev}", json=True, host=host)[0]
    for name, state in feats.items():
        if current[name]["active"] != state:
            raise KsftXfailEx(f"{name} was not set to {s2n[state]}")
