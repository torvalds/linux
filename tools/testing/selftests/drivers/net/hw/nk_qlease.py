#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import errno
import re
import time
import threading
from os import path
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
    NetDrvContEnv,
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
    bkg,
    cmd,
    defer,
    ethtool,
    ip,
    rand_port,
    wait_port_listen,
)
from lib.py import KsftSkipEx, CmdExitFailure


def set_flow_rule(cfg):
    output = ethtool(
        f"-N {cfg.ifname} flow-type tcp6 dst-port {cfg.port} action {cfg.src_queue}"
    ).stdout
    values = re.search(r"ID (\d+)", output).group(1)
    return int(values)


def create_netkit(rxqueues):
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
                    "mode": "l2",
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
    time.sleep(0.1)
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
    time.sleep(0.1)
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
    time.sleep(0.1)
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


def test_iou_zcrx(cfg) -> None:
    cfg.require_ipver("6")
    ethnl = EthtoolFamily()

    rings = ethnl.rings_get({"header": {"dev-index": cfg.ifindex}})
    rx_rings = rings["rx"]
    hds_thresh = rings.get("hds-thresh", 0)

    ethnl.rings_set(
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "enabled",
            "hds-thresh": 0,
            "rx": 64,
        }
    )
    defer(
        ethnl.rings_set,
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "unknown",
            "hds-thresh": hds_thresh,
            "rx": rx_rings,
        },
    )

    ethtool(f"-X {cfg.ifname} equal {cfg.src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    flow_rule_id = set_flow_rule(cfg)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

    rx_cmd = f"ip netns exec {cfg.netns.name} {cfg.bin_local} -s -p {cfg.port} -i {cfg._nk_guest_ifname} -q {cfg.nk_queue}"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.nk_guest_ipv6} -p {cfg.port} -l 12840"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(cfg.port, proto="tcp", ns=cfg.netns)
        cmd(tx_cmd, host=cfg.remote)


def test_attrs(cfg) -> None:
    cfg.require_ipver("6")
    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
    )

    ksft_eq(queue_info["id"], cfg.src_queue)
    ksft_eq(queue_info["type"], "rx")
    ksft_eq(queue_info["ifindex"], cfg.ifindex)

    ksft_in("lease", queue_info)
    lease = queue_info["lease"]
    ksft_eq(lease["ifindex"], cfg.nk_guest_ifindex)
    ksft_eq(lease["queue"]["id"], cfg.nk_queue)
    ksft_eq(lease["queue"]["type"], "rx")
    ksft_in("netns-id", lease)


def test_attach_xdp_with_mp(cfg) -> None:
    cfg.require_ipver("6")
    ethnl = EthtoolFamily()

    rings = ethnl.rings_get({"header": {"dev-index": cfg.ifindex}})
    rx_rings = rings["rx"]
    hds_thresh = rings.get("hds-thresh", 0)

    ethnl.rings_set(
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "enabled",
            "hds-thresh": 0,
            "rx": 64,
        }
    )
    defer(
        ethnl.rings_set,
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "unknown",
            "hds-thresh": hds_thresh,
            "rx": rx_rings,
        },
    )

    ethtool(f"-X {cfg.ifname} equal {cfg.src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    netdevnl = NetdevFamily()

    rx_cmd = f"ip netns exec {cfg.netns.name} {cfg.bin_local} -s -p {cfg.port} -i {cfg._nk_guest_ifname} -q {cfg.nk_queue}"
    with bkg(rx_cmd):
        wait_port_listen(cfg.port, proto="tcp", ns=cfg.netns)

        time.sleep(0.1)
        queue_info = netdevnl.queue_get(
            {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
        )
        ksft_in("io-uring", queue_info)

        prog = cfg.net_lib_dir / "xdp_dummy.bpf.o"
        with ksft_raises(CmdExitFailure):
            ip(f"link set dev {cfg.ifname} xdp obj {prog} sec xdp.frags")

    time.sleep(0.1)
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
    )
    ksft_not_in("io-uring", queue_info)


def test_destroy(cfg) -> None:
    cfg.require_ipver("6")
    ethnl = EthtoolFamily()

    rings = ethnl.rings_get({"header": {"dev-index": cfg.ifindex}})
    rx_rings = rings["rx"]
    hds_thresh = rings.get("hds-thresh", 0)

    ethnl.rings_set(
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "enabled",
            "hds-thresh": 0,
            "rx": 64,
        }
    )
    defer(
        ethnl.rings_set,
        {
            "header": {"dev-index": cfg.ifindex},
            "tcp-data-split": "unknown",
            "hds-thresh": hds_thresh,
            "rx": rx_rings,
        },
    )

    ethtool(f"-X {cfg.ifname} equal {cfg.src_queue}")
    defer(ethtool, f"-X {cfg.ifname} default")

    rx_cmd = f"ip netns exec {cfg.netns.name} {cfg.bin_local} -s -p {cfg.port} -i {cfg._nk_guest_ifname} -q {cfg.nk_queue}"
    rx_proc = cmd(rx_cmd, background=True)
    wait_port_listen(cfg.port, proto="tcp", ns=cfg.netns)

    netdevnl = NetdevFamily()
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
    )
    ksft_in("io-uring", queue_info)

    # ip link del will wait for all refs to drop first, but iou-zcrx is holding
    # onto a ref. Terminate iou-zcrx async via a thread after a delay.
    kill_timer = threading.Timer(1, rx_proc.proc.terminate)
    kill_timer.start()

    ip(f"link del dev {cfg._nk_host_ifname}")
    kill_timer.join()
    cfg._nk_host_ifname = None
    cfg._nk_guest_ifname = None

    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
    )
    ksft_not_in("io-uring", queue_info)

    cmd(f"tc filter del dev {cfg.ifname} ingress pref {cfg._bpf_prog_pref}")
    cfg._tc_attached = False

    flow_rule_id = set_flow_rule(cfg)
    defer(ethtool, f"-N {cfg.ifname} delete {flow_rule_id}")

    rx_cmd = f"{cfg.bin_local} -s -p {cfg.port} -i {cfg.ifname} -q {cfg.src_queue}"
    tx_cmd = f"{cfg.bin_remote} -c -h {cfg.addr_v['6']} -p {cfg.port} -l 12840"
    with bkg(rx_cmd, exit_wait=True):
        wait_port_listen(cfg.port, proto="tcp")
        cmd(tx_cmd, host=cfg.remote)
    # Short delay since iou cleanup is async and takes a bit of time.
    time.sleep(0.1)
    queue_info = netdevnl.queue_get(
        {"ifindex": cfg.ifindex, "id": cfg.src_queue, "type": "rx"}
    )
    ksft_not_in("io-uring", queue_info)


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
        ],
        args=(netns,),
    )

    cmd("ip netns del init", fail=False)
    del netns

    with NetDrvContEnv(__file__, rxqueues=2) as cfg:
        cfg.bin_local = path.abspath(
            path.dirname(__file__) + "/../../../drivers/net/hw/iou-zcrx"
        )
        cfg.bin_remote = cfg.remote.deploy(cfg.bin_local)
        cfg.port = rand_port()

        ethnl = EthtoolFamily()
        channels = ethnl.channels_get({"header": {"dev-index": cfg.ifindex}})
        channels = channels["combined-count"]
        if channels < 2:
            raise KsftSkipEx("Test requires NETIF with at least 2 combined channels")

        cfg.src_queue = channels - 1

        with NetNSEnter(str(cfg.netns)):
            netdevnl = NetdevFamily()
            bind_result = netdevnl.queue_create(
                {
                    "ifindex": cfg.nk_guest_ifindex,
                    "type": "rx",
                    "lease": {
                        "ifindex": cfg.ifindex,
                        "queue": {"id": cfg.src_queue, "type": "rx"},
                        "netns-id": 0,
                    },
                }
            )
            cfg.nk_queue = bind_result["id"]

        # test_destroy must be last because it destroys the netkit devices
        ksft_run(
            [test_iou_zcrx, test_attrs, test_attach_xdp_with_mp, test_destroy],
            args=(cfg,),
        )
    ksft_exit()


if __name__ == "__main__":
    main()
