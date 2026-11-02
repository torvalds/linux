#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"""Tests for the netdev instance lock."""

from lib.py import ksft_run, ksft_exit
from lib.py import ip
from lib.py import NetNS


def test_unreg_order() -> None:
    """Dismantle two netns holding the same two kinds of ops locked device,
    registered in opposite order.

    unregister_netdevice_many_notify() takes the instance lock of every
    device in the batch and holds them all at once, so the two kinds end up
    nested one way round for the first netns and the other way round for
    the second.
    """
    with NetNS() as ns1, NetNS() as ns2:
        net1, net2 = str(ns1), str(ns2)

        ip("link add du0 type dummy", ns=net1)
        ip("link add nk0 type netkit peer name nk1", ns=net1)

        ip("link add nk0 type netkit peer name nk1", ns=net2)
        ip("link add du0 type dummy", ns=net2)

        # only devices which are up get locked during unregister
        for net in (net1, net2):
            for dev in ("du0", "nk0", "nk1"):
                ip(f"link set {dev} up", ns=net)


def main() -> None:
    """Ksft boilerplate main."""
    ksft_run([test_unreg_order])
    ksft_exit()


if __name__ == "__main__":
    main()
