#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import errno
import time
from lib.py import (
    ksft_run,
    ksft_exit,
    ksft_eq,
    ksft_ne,
    ksft_in,
    ksft_not_in,
    ksft_raises,
)
from lib.py import (
    NetNS,
    NetNSEnter,
    EthtoolFamily,
    NetdevFamily,
    RtnlFamily,
    NetdevSimDev,
)
from lib.py import (
    NlError,
    Netlink,
    cmd,
    defer,
    ip,
)


def wait_until(cond, timeout=2.0, interval=0.05):
    deadline = time.monotonic() + timeout
    while not cond():
        if time.monotonic() >= deadline:
            return
        time.sleep(interval)


def create_netkit(rxqueues, mode="l2"):
    all_links = ip("-d link show", json=True)
    old_idxs = {
        link["ifindex"]
        for link in all_links
        if link.get("linkinfo", {}).get("info_kind") == "netkit"
    }

    rtnl = RtnlFamily()
    rtnl.newlink(
        {
            "linkinfo": {
                "kind": "netkit",
                "data": {
                    "mode": mode,
                    "policy": "forward",
                    "peer-policy": "forward",
                },
            },
            "num-rx-queues": rxqueues,
        },
        flags=[Netlink.NLM_F_CREATE, Netlink.NLM_F_EXCL],
    )

    all_links = ip("-d link show", json=True)
    nk_links = [
        link
        for link in all_links
        if link.get("linkinfo", {}).get("info_kind") == "netkit"
        and link["ifindex"] not in old_idxs
    ]
    nk_links.sort(key=lambda x: x["ifindex"])
    return (
        nk_links[1]["ifname"],
        nk_links[1]["ifindex"],
        nk_links[0]["ifname"],
        nk_links[0]["ifindex"],
    )


def create_netkit_single(rxqueues):
    rtnl = RtnlFamily()
    rtnl.newlink(
        {
            "linkinfo": {
                "kind": "netkit",
                "data": {
                    "mode": "l2",
                    "pairing": "single",
                },
            },
            "num-rx-queues": rxqueues,
        },
        flags=[Netlink.NLM_F_CREATE, Netlink.NLM_F_EXCL],
    )

    all_links = ip("-d link show", json=True)
    nk_links = [
        link
        for link in all_links
        if link.get("linkinfo", {}).get("info_kind") == "netkit"
        and "UP" not in link.get("flags", [])
    ]
    return nk_links[0]["ifname"], nk_links[0]["ifindex"]


def test_remove_phys(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        result = netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        nk_queue_id = result["id"]

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["ifindex"], nk_guest_idx)
    ksft_eq(queue_info["lease"]["queue"]["id"], nk_queue_id)

    nsimdev.remove()
    wait_until(lambda: cmd(f"ip link show dev {nk_host}", fail=False).ret != 0)
    ret = cmd(f"ip link show dev {nk_host}", fail=False)
    ksft_ne(ret.ret, 0)


def test_double_lease(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=3)
    defer(cmd, f"ip link del dev {nk_host}")

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        result = netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        ksft_eq(result["id"], 1)

        with ksft_raises(NlError) as e:
            netdevnl.queue_create(
                {
                    "ifindex": nk_guest_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": nsim.ifindex,
                        "queue": {"id": src_queue, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.EBUSY)


def test_virtual_lessor(netns) -> None:
    nk_host_a, _, nk_guest_a, nk_guest_a_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_a}")
    ip(f"link set dev {nk_host_a} up")
    ip(f"link set dev {nk_guest_a} up")

    nk_host_b, _, nk_guest_b, nk_guest_b_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_b}")

    ip(f"link set dev {nk_guest_b} netns {netns.name}")
    ip(f"link set dev {nk_host_b} up")
    ip(f"link set dev {nk_guest_b} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        with ksft_raises(NlError) as e:
            netdevnl.queue_create(
                {
                    "ifindex": nk_guest_b_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": nk_guest_a_idx,
                        "queue": {"id": 0, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)


def test_phys_lessee(_netns) -> None:
    nsimdev_a = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev_a.remove)
    nsim_a = nsimdev_a.nsims[0]
    ip(f"link set dev {nsim_a.ifname} up")

    nsimdev_b = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev_b.remove)
    nsim_b = nsimdev_b.nsims[0]
    ip(f"link set dev {nsim_b.ifname} up")

    netdevnl = NetdevFamily()
    with ksft_raises(NlError) as e:
        netdevnl.queue_create(
            {
                "ifindex": nsim_a.ifindex,
                "type": "rx",
                "lease": {
                    "ifindex": nsim_b.ifindex,
                    "queue": {"id": 0, "type": "rx"},
                },
            }
        )
    ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)


def test_different_lessors(netns) -> None:
    nsimdev_a = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev_a.remove)
    nsim_a = nsimdev_a.nsims[0]
    ip(f"link set dev {nsim_a.ifname} up")

    nsimdev_b = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev_b.remove)
    nsim_b = nsimdev_b.nsims[0]
    ip(f"link set dev {nsim_b.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=3)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim_a.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

        with ksft_raises(NlError) as e:
            netdevnl.queue_create(
                {
                    "ifindex": nk_guest_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": nsim_b.ifindex,
                        "queue": {"id": 1, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.EOPNOTSUPP)


def test_queue_out_of_range(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        with ksft_raises(NlError) as e:
            netdevnl.queue_create(
                {
                    "ifindex": nk_guest_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": nsim.ifindex,
                        "queue": {"id": 2, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.ERANGE)


def test_resize_leased(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    ethnl = EthtoolFamily()
    with ksft_raises(NlError) as e:
        ethnl.channels_set({"header": {"dev-index": nsim.ifindex}, "combined-count": 1})
    ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)


def test_self_lease(_netns) -> None:
    nk_host, _, _, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    netdevnl = NetdevFamily()
    with ksft_raises(NlError) as e:
        netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nk_guest_idx,
                    "queue": {"id": 0, "type": "rx"},
                },
            }
        )
    ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)


def test_veth_queue_create(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    ip("link add veth0 type veth peer name veth1")
    defer(cmd, "ip link del dev veth0", fail=False)

    all_links = ip("-d link show", json=True)
    veth_peer = [
        link
        for link in all_links
        if link.get("ifname") == "veth1"
    ]
    veth_peer_idx = veth_peer[0]["ifindex"]

    ip(f"link set dev veth1 netns {netns.name}")
    ip("link set dev veth0 up")
    ip("link set dev veth1 up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        with ksft_raises(NlError) as e:
            netdevnl.queue_create(
                {
                    "ifindex": veth_peer_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": nsim.ifindex,
                        "queue": {"id": 1, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)


def test_create_tx_type(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        with ksft_raises(NlError) as e:
            netdevnl.queue_create(
                {
                    "ifindex": nk_guest_idx,
                    "type": "tx",
                    "lease": {
                        "ifindex": nsim.ifindex,
                        "queue": {"id": 1, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)


def test_create_primary(_netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, nk_host_idx, _, _ = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_host} up")

    netdevnl = NetdevFamily()
    with ksft_raises(NlError) as e:
        netdevnl.queue_create(
            {
                "ifindex": nk_host_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                },
            }
        )
    ksft_eq(e.exception.nl_msg.error, -errno.EOPNOTSUPP)


def test_create_limit(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=1)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        with ksft_raises(NlError) as e:
            netdevnl.queue_create(
                {
                    "ifindex": nk_guest_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": nsim.ifindex,
                        "queue": {"id": 1, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)


def test_link_flap_phys(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}")

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        result = netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        nk_queue_id = result["id"]

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["queue"]["id"], nk_queue_id)

    # Link flap the physical device
    ip(f"link set dev {nsim.ifname} down")
    ip(f"link set dev {nsim.ifname} up")

    # Verify lease survives the flap
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["queue"]["id"], nk_queue_id)


def test_queue_get_virtual(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}")

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        result = netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        nk_queue_id = result["id"]

        # queue-get on virtual device's leased queue should not show lease
        # info (lease info is only shown from the physical device's side)
        queue_info = netdevnl.queue_get(
            {"ifindex": nk_guest_idx, "id": nk_queue_id, "type": "rx"}
        )
        ksft_eq(queue_info["id"], nk_queue_id)
        ksft_eq(queue_info["ifindex"], nk_guest_idx)
        ksft_not_in("lease", queue_info)

        # Default queue (not leased) also has no lease info
        queue_info = netdevnl.queue_get(
            {"ifindex": nk_guest_idx, "id": 0, "type": "rx"}
        )
        ksft_not_in("lease", queue_info)


def test_remove_virt_first(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        result = netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        ksft_eq(result["id"], 1)

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["queue"]["id"], result["id"])

    # Delete netkit (virtual device removed first, physical stays)
    cmd(f"ip link del dev {nk_host}")

    # Verify lease is cleaned up on physical device
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_not_in("lease", queue_info)


def test_multiple_leases(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=3)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=4)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        r1 = netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        r2 = netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 2, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    ksft_eq(r1["id"], 1)
    ksft_eq(r2["id"], 2)

    # Verify both leases visible on physical device
    netdevnl = NetdevFamily()
    q1 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    q2 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 2, "type": "rx"}
    )
    ksft_in("lease", q1)
    ksft_in("lease", q2)
    ksft_eq(q1["lease"]["ifindex"], nk_guest_idx)
    ksft_eq(q2["lease"]["ifindex"], nk_guest_idx)
    ksft_eq(q1["lease"]["queue"]["id"], r1["id"])
    ksft_eq(q2["lease"]["queue"]["id"], r2["id"])


def test_lease_queue_tx_type(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        with ksft_raises(NlError) as e:
            netdevnl.queue_create(
                {
                    "ifindex": nk_guest_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": nsim.ifindex,
                        "queue": {"id": 1, "type": "tx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)


def test_invalid_netns(netns) -> None:
    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        with ksft_raises(NlError) as e:
            netdevnl.queue_create(
                {
                    "ifindex": nk_guest_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": 1,
                        "queue": {"id": 0, "type": "rx"},
                        "netns-id": 999,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.ENONET)


def test_invalid_phys_ifindex(netns) -> None:
    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        with ksft_raises(NlError) as e:
            netdevnl.queue_create(
                {
                    "ifindex": nk_guest_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": 99999,
                        "queue": {"id": 0, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.ENODEV)


def test_multi_netkit_remove_phys(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=3)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    # Create two netkit pairs, each leasing a different physical queue
    nk_host_a, _, nk_guest_a, nk_guest_a_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_a}", fail=False)

    nk_host_b, _, nk_guest_b, nk_guest_b_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_b}", fail=False)

    ip(f"link set dev {nk_guest_a} netns {netns.name}")
    ip(f"link set dev {nk_host_a} up")
    ip(f"link set dev {nk_guest_a} up", ns=netns)

    ip(f"link set dev {nk_guest_b} netns {netns.name}")
    ip(f"link set dev {nk_host_b} up")
    ip(f"link set dev {nk_guest_b} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        netdevnl.queue_create(
            {
                "ifindex": nk_guest_a_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        netdevnl.queue_create(
            {
                "ifindex": nk_guest_b_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 2, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    # Removing the physical device should take down both netkit pairs
    nsimdev.remove()
    wait_until(lambda: cmd(f"ip link show dev {nk_host_a}", fail=False).ret != 0
                       and cmd(f"ip link show dev {nk_host_b}", fail=False).ret != 0)
    ret = cmd(f"ip link show dev {nk_host_a}", fail=False)
    ksft_ne(ret.ret, 0)
    ret = cmd(f"ip link show dev {nk_host_b}", fail=False)
    ksft_ne(ret.ret, 0)


def test_single_remove_phys(_netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_name, nk_idx = create_netkit_single(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_name}", fail=False)

    ip(f"link set dev {nk_name} up")

    netdevnl = NetdevFamily()
    netdevnl.queue_create(
        {
            "ifindex": nk_idx,
            "type": "rx",
            "lease": {
                "ifindex": nsim.ifindex,
                "queue": {"id": 1, "type": "rx"},
            },
        }
    )

    # Removing the physical device should take down the single netkit device
    nsimdev.remove()
    wait_until(lambda: cmd(f"ip link show dev {nk_name}", fail=False).ret != 0)
    ret = cmd(f"ip link show dev {nk_name}", fail=False)
    ksft_ne(ret.ret, 0)


def test_link_flap_virt(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}")

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        result = netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        nk_queue_id = result["id"]

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["queue"]["id"], nk_queue_id)

    # Link flap the virtual (netkit) device
    ip(f"link set dev {nk_guest} down", ns=netns)
    ip(f"link set dev {nk_guest} up", ns=netns)

    # Verify lease survives the virtual device flap
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["queue"]["id"], nk_queue_id)


def test_phys_queue_no_lease(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}")

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    # Physical queue 0 (not leased) should have no lease info
    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 0, "type": "rx"}
    )
    ksft_not_in("lease", queue_info)

    # Physical queue 1 (leased) should have lease info
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    ksft_in("lease", queue_info)


def test_same_ns_lease(_netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_name, nk_idx = create_netkit_single(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_name}", fail=False)

    ip(f"link set dev {nk_name} up")

    netdevnl = NetdevFamily()
    result = netdevnl.queue_create(
        {
            "ifindex": nk_idx,
            "type": "rx",
            "lease": {
                "ifindex": nsim.ifindex,
                "queue": {"id": 1, "type": "rx"},
            },
        }
    )
    ksft_eq(result["id"], 1)

    # Same namespace: lease info should NOT have netns-id
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["ifindex"], nk_idx)
    ksft_eq(queue_info["lease"]["queue"]["id"], result["id"])
    ksft_not_in("netns-id", queue_info["lease"])


def test_resize_after_unlease(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    # Resize should fail while lease is active
    ethnl = EthtoolFamily()
    with ksft_raises(NlError) as e:
        ethnl.channels_set({"header": {"dev-index": nsim.ifindex}, "combined-count": 1})
    ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)

    # Delete netkit, clearing the lease
    cmd(f"ip link del dev {nk_host}")

    # Resize should now succeed
    ethnl.channels_set({"header": {"dev-index": nsim.ifindex}, "combined-count": 1})


def test_lease_queue_zero(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        result = netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 0, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        ksft_eq(result["id"], 1)

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 0, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["queue"]["id"], result["id"])


def test_release_and_reuse(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    src_queue = 1

    # First lease
    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)

    # Delete netkit, freeing the lease
    cmd(f"ip link del dev {nk_host}")

    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_not_in("lease", queue_info)

    # Re-create netkit and lease the same physical queue again
    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)):
        netdevnl = NetdevFamily()
        result = netdevnl.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        ksft_eq(result["id"], 1)

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["queue"]["id"], result["id"])


def test_two_netkits_same_queue(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host_a, _, nk_guest_a, nk_guest_a_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_a}", fail=False)

    nk_host_b, _, nk_guest_b, nk_guest_b_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_b}", fail=False)

    ip(f"link set dev {nk_guest_a} netns {netns.name}")
    ip(f"link set dev {nk_host_a} up")
    ip(f"link set dev {nk_guest_a} up", ns=netns)

    ip(f"link set dev {nk_guest_b} netns {netns.name}")
    ip(f"link set dev {nk_host_b} up")
    ip(f"link set dev {nk_guest_b} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_a_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

        with ksft_raises(NlError) as e:
            netdevnl_ns.queue_create(
                {
                    "ifindex": nk_guest_b_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": nsim.ifindex,
                        "queue": {"id": src_queue, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.EBUSY)


def test_l3_mode_lease(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2, mode="l3")
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        result = netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        ksft_eq(result["id"], 1)

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["ifindex"], nk_guest_idx)
    ksft_eq(queue_info["lease"]["queue"]["id"], result["id"])


def test_single_double_lease(_netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_name, nk_idx = create_netkit_single(rxqueues=3)
    defer(cmd, f"ip link del dev {nk_name}", fail=False)

    ip(f"link set dev {nk_name} up")

    netdevnl = NetdevFamily()
    result = netdevnl.queue_create(
        {
            "ifindex": nk_idx,
            "type": "rx",
            "lease": {
                "ifindex": nsim.ifindex,
                "queue": {"id": 1, "type": "rx"},
            },
        }
    )
    ksft_eq(result["id"], 1)

    with ksft_raises(NlError) as e:
        netdevnl.queue_create(
            {
                "ifindex": nk_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                },
            }
        )
    ksft_eq(e.exception.nl_msg.error, -errno.EBUSY)


def test_single_different_lessors(_netns) -> None:
    nsimdev_a = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev_a.remove)
    nsim_a = nsimdev_a.nsims[0]
    ip(f"link set dev {nsim_a.ifname} up")

    nsimdev_b = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev_b.remove)
    nsim_b = nsimdev_b.nsims[0]
    ip(f"link set dev {nsim_b.ifname} up")

    nk_name, nk_idx = create_netkit_single(rxqueues=3)
    defer(cmd, f"ip link del dev {nk_name}", fail=False)

    ip(f"link set dev {nk_name} up")

    netdevnl = NetdevFamily()
    netdevnl.queue_create(
        {
            "ifindex": nk_idx,
            "type": "rx",
            "lease": {
                "ifindex": nsim_a.ifindex,
                "queue": {"id": 1, "type": "rx"},
            },
        }
    )

    with ksft_raises(NlError) as e:
        netdevnl.queue_create(
            {
                "ifindex": nk_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim_b.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                },
            }
        )
    ksft_eq(e.exception.nl_msg.error, -errno.EOPNOTSUPP)


def test_cross_ns_netns_id(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_in("netns-id", queue_info["lease"])


def test_delete_guest_netns(_netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    test_ns = NetNS()
    ip("netns set init 0", ns=test_ns)
    ip("link set lo up", ns=test_ns)

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {test_ns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=test_ns)

    src_queue = 1
    with NetNSEnter(str(test_ns)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)

    del test_ns
    wait_until(lambda: "lease" not in netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}))

    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_not_in("lease", queue_info)

    ret = cmd(f"ip link show dev {nk_host}", fail=False)
    ksft_ne(ret.ret, 0)


def test_move_guest_netns(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        result = netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        nk_queue_id = result["id"]

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["queue"]["id"], nk_queue_id)

    new_ns = NetNS()
    defer(new_ns.__del__)
    ip(f"link set dev {nk_guest} netns {new_ns.name}", ns=netns)

    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["queue"]["id"], nk_queue_id)


def test_resize_phys_no_reduction(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    ethnl = EthtoolFamily()
    ethnl.channels_set(
        {"header": {"dev-index": nsim.ifindex}, "combined-count": 2}
    )

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    ksft_in("lease", queue_info)


def test_delete_one_netkit_of_two(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=3)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host_a, _, nk_guest_a, nk_guest_a_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_a}", fail=False)

    nk_host_b, _, nk_guest_b, nk_guest_b_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_b}", fail=False)

    ip(f"link set dev {nk_guest_a} netns {netns.name}")
    ip(f"link set dev {nk_host_a} up")
    ip(f"link set dev {nk_guest_a} up", ns=netns)

    ip(f"link set dev {nk_guest_b} netns {netns.name}")
    ip(f"link set dev {nk_host_b} up")
    ip(f"link set dev {nk_guest_b} up", ns=netns)

    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_a_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_b_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 2, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    netdevnl = NetdevFamily()
    q1 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    q2 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 2, "type": "rx"}
    )
    ksft_in("lease", q1)
    ksft_in("lease", q2)

    cmd(f"ip link del dev {nk_host_a}")

    q1 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    q2 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 2, "type": "rx"}
    )
    ksft_not_in("lease", q1)
    ksft_in("lease", q2)


def test_bind_rx_leased_phys_queue(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    netdevnl = NetdevFamily()
    with ksft_raises(NlError) as e:
        netdevnl.bind_rx(
            {
                "ifindex": nsim.ifindex,
                "fd": 0,
                "queues": [
                    {"id": 0, "type": "rx"},
                    {"id": 1, "type": "rx"},
                ],
            }
        )
    ksft_eq(e.exception.nl_msg.error, -errno.EOPNOTSUPP)


def test_resize_phys_shrink_past_leased(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=4)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    ethnl = EthtoolFamily()

    # Shrink past the leased queue — only queue 3 removed, queue 1 untouched
    ethnl.channels_set(
        {"header": {"dev-index": nsim.ifindex}, "combined-count": 3}
    )

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    ksft_in("lease", queue_info)

    # Shrink further — queue 2 removed, queue 1 still untouched
    ethnl.channels_set(
        {"header": {"dev-index": nsim.ifindex}, "combined-count": 2}
    )

    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    ksft_in("lease", queue_info)

    # Shrink into the leased queue — queue 1 is busy, must fail
    with ksft_raises(NlError) as e:
        ethnl.channels_set(
            {"header": {"dev-index": nsim.ifindex}, "combined-count": 1}
        )
    ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)


def test_resize_virt_not_supported(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, nk_host_idx, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    # Channel resize on the netkit host must fail — not supported
    ethnl = EthtoolFamily()
    with ksft_raises(NlError) as e:
        ethnl.channels_set(
            {"header": {"dev-index": nk_host_idx}, "combined-count": 1}
        )
    ksft_eq(e.exception.nl_msg.error, -errno.EOPNOTSUPP)

    # Lease must be intact
    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    ksft_in("lease", queue_info)


def test_lease_devices_down(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")

    # Create lease while both physical and virtual devices are down
    src_queue = 1
    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        result = netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        ksft_eq(result["id"], 1)

    # Bring devices up before queue_get: netdevsim only instantiates NAPIs in
    # ndo_open, and netdev-genl queue_get returns -ENOENT without a NAPI.
    ip(f"link set dev {nsim.ifname} up")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)
    ksft_eq(queue_info["lease"]["queue"]["id"], result["id"])


def test_lease_capacity_exhaustion(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=4)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    # rxqueues=3 means num_rx_queues=3, real_num_rx_queues starts at 1.
    # Can create 2 leased queues (real goes 1->2->3) but not a 3rd (3->4 > 3).
    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=3)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        r1 = netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        ksft_eq(r1["id"], 1)

        r2 = netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 2, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        ksft_eq(r2["id"], 2)

        # Third lease fails — netkit queue capacity exhausted
        with ksft_raises(NlError) as e:
            netdevnl_ns.queue_create(
                {
                    "ifindex": nk_guest_idx,
                    "type": "rx",
                    "lease": {
                        "ifindex": nsim.ifindex,
                        "queue": {"id": 3, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
        ksft_eq(e.exception.nl_msg.error, -errno.EINVAL)

    # Verify the two successful leases are intact
    netdevnl = NetdevFamily()
    q1 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    q2 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 2, "type": "rx"}
    )
    ksft_in("lease", q1)
    ksft_in("lease", q2)


def test_resize_phys_up(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=3)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    # Shrink nsim first so we have room to grow
    ethnl = EthtoolFamily()
    ethnl.channels_set(
        {"header": {"dev-index": nsim.ifindex}, "combined-count": 2}
    )

    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    # Grow channels — should succeed since leased queue is not removed
    ethnl.channels_set(
        {"header": {"dev-index": nsim.ifindex}, "combined-count": 3}
    )

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    ksft_in("lease", queue_info)

    # New queue 2 should exist without a lease
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 2, "type": "rx"}
    )
    ksft_not_in("lease", queue_info)


def test_multi_ns_lease(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=3)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    ns_b = NetNS()
    defer(ns_b.__del__)
    ip("netns set init 0", ns=ns_b)
    ip("link set lo up", ns=ns_b)

    # First netkit pair, guest in netns
    nk_host_a, _, nk_guest_a, nk_guest_a_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_a}", fail=False)
    ip(f"link set dev {nk_guest_a} netns {netns.name}")
    ip(f"link set dev {nk_host_a} up")
    ip(f"link set dev {nk_guest_a} up", ns=netns)

    # Second netkit pair, guest in ns_b
    nk_host_b, _, nk_guest_b, nk_guest_b_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_b}", fail=False)
    ip(f"link set dev {nk_guest_b} netns {ns_b.name}")
    ip(f"link set dev {nk_host_b} up")
    ip(f"link set dev {nk_guest_b} up", ns=ns_b)

    # Lease from netns
    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        result = netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_a_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        ksft_eq(result["id"], 1)

    # Lease from ns_b (different namespace, same physical device)
    with NetNSEnter(str(ns_b)), NetdevFamily() as netdevnl_ns:
        result = netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_b_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 2, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )
        ksft_eq(result["id"], 1)

    # Verify both leases from the physical side
    netdevnl = NetdevFamily()
    q1 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    q2 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 2, "type": "rx"}
    )
    ksft_in("lease", q1)
    ksft_in("lease", q2)
    ksft_eq(q1["lease"]["ifindex"], nk_guest_a_idx)
    ksft_eq(q2["lease"]["ifindex"], nk_guest_b_idx)


def test_multi_ns_delete_one(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=3)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    ns_b = NetNS()
    ip("netns set init 0", ns=ns_b)
    ip("link set lo up", ns=ns_b)

    # First netkit pair, guest in netns (ns_a)
    nk_host_a, _, nk_guest_a, nk_guest_a_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_a}", fail=False)
    ip(f"link set dev {nk_guest_a} netns {netns.name}")
    ip(f"link set dev {nk_host_a} up")
    ip(f"link set dev {nk_guest_a} up", ns=netns)

    # Second netkit pair, guest in ns_b
    nk_host_b, _, nk_guest_b, nk_guest_b_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host_b}", fail=False)

    ip(f"link set dev {nk_guest_b} netns {ns_b.name}")
    ip(f"link set dev {nk_host_b} up")
    ip(f"link set dev {nk_guest_b} up", ns=ns_b)

    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_a_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 1, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    with NetNSEnter(str(ns_b)), NetdevFamily() as netdevnl_ns:
        netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_b_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": 2, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )

    netdevnl = NetdevFamily()
    q1 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    q2 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 2, "type": "rx"}
    )
    ksft_in("lease", q1)
    ksft_in("lease", q2)

    # Delete ns_b — destroys nk_guest_b, triggers unlease of queue 2
    del ns_b
    wait_until(lambda: "lease" not in netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 2, "type": "rx"}))

    # ns_a's lease on queue 1 must survive
    q1 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 1, "type": "rx"}
    )
    ksft_in("lease", q1)
    ksft_eq(q1["lease"]["ifindex"], nk_guest_a_idx)

    # ns_b's lease on queue 2 must be gone
    q2 = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": 2, "type": "rx"}
    )
    ksft_not_in("lease", q2)

    # nk_host_b should be gone too (phys removal cascades to netkit pair)
    ret = cmd(f"ip link show dev {nk_host_b}", fail=False)
    ksft_ne(ret.ret, 0)


def test_move_phys_netns(netns) -> None:
    nsimdev = NetdevSimDev(port_count=1, queue_count=2)
    defer(nsimdev.remove)
    nsim = nsimdev.nsims[0]
    ip(f"link set dev {nsim.ifname} up")

    nk_host, _, nk_guest, nk_guest_idx = create_netkit(rxqueues=2)
    defer(cmd, f"ip link del dev {nk_host}", fail=False)

    ip(f"link set dev {nk_guest} netns {netns.name}")
    ip(f"link set dev {nk_host} up")
    ip(f"link set dev {nk_guest} up", ns=netns)

    src_queue = 1
    with NetNSEnter(str(netns)), NetdevFamily() as netdevnl_ns:
        nk_queue_id = netdevnl_ns.queue_create(
            {
                "ifindex": nk_guest_idx,
                "type": "rx",
                "lease": {
                    "ifindex": nsim.ifindex,
                    "queue": {"id": src_queue, "type": "rx"},
                    "netns-id": 0,
                },
            }
        )["id"]

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": nsim.ifindex, "id": src_queue, "type": "rx"}
    )
    ksft_in("lease", queue_info)

    # Move the physical device to a new namespace. Move it back to init_net
    # on cleanup before the other defers fire (new_ns deletion, nsimdev.remove)
    # so nsim lives in a stable namespace when they run.
    new_ns = NetNS()
    defer(new_ns.__del__)
    ip(f"link set dev {nsim.ifname} netns {new_ns.name}")
    defer(ip, f"link set dev {nsim.ifname} netns init", ns=new_ns)

    # Physical device is now in new_ns — find its ifindex there
    all_links = ip("-d link show", json=True, ns=new_ns)
    nsim_in_new = [lnk for lnk in all_links if lnk.get("ifname") == nsim.ifname]
    new_ifindex = nsim_in_new[0]["ifindex"]

    # Moving a device across netns brings it admin-down; bring it back up so
    # netdevsim re-creates the NAPI (netdev-genl queue_get needs it).
    ip(f"link set dev {nsim.ifname} up", ns=new_ns)

    # Verify lease survived the namespace move
    with NetNSEnter(str(new_ns)), NetdevFamily() as netdevnl_ns:
        queue_info = netdevnl_ns.queue_get(
            {"ifindex": new_ifindex, "id": src_queue, "type": "rx"}
        )
        ksft_in("lease", queue_info)
        ksft_eq(queue_info["lease"]["queue"]["id"], nk_queue_id)


def main() -> None:
    netns = NetNS()
    cmd("ip netns attach init 1")
    ip("netns set init 0", ns=netns)
    ip("link set lo up", ns=netns)

    ksft_run(
        [
            test_remove_phys,
            test_double_lease,
            test_virtual_lessor,
            test_phys_lessee,
            test_different_lessors,
            test_queue_out_of_range,
            test_resize_leased,
            test_self_lease,
            test_create_tx_type,
            test_create_primary,
            test_create_limit,
            test_link_flap_phys,
            test_queue_get_virtual,
            test_remove_virt_first,
            test_multiple_leases,
            test_lease_queue_tx_type,
            test_invalid_netns,
            test_invalid_phys_ifindex,
            test_multi_netkit_remove_phys,
            test_single_remove_phys,
            test_link_flap_virt,
            test_phys_queue_no_lease,
            test_same_ns_lease,
            test_resize_after_unlease,
            test_lease_queue_zero,
            test_release_and_reuse,
            test_veth_queue_create,
            test_two_netkits_same_queue,
            test_l3_mode_lease,
            test_single_double_lease,
            test_single_different_lessors,
            test_cross_ns_netns_id,
            test_delete_guest_netns,
            test_move_guest_netns,
            test_resize_phys_no_reduction,
            test_delete_one_netkit_of_two,
            test_bind_rx_leased_phys_queue,
            test_resize_phys_shrink_past_leased,
            test_resize_virt_not_supported,
            test_lease_devices_down,
            test_lease_capacity_exhaustion,
            test_resize_phys_up,
            test_multi_ns_lease,
            test_multi_ns_delete_one,
            test_move_phys_netns,
        ],
        args=(netns,),
    )

    cmd("ip netns del init", fail=False)
    ksft_exit()


if __name__ == "__main__":
    main()
