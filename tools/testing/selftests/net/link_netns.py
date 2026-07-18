#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import time

from lib.py import ksft_run, ksft_exit, ksft_eq, ksft_true
from lib.py import ip
from lib.py import NetNS, NetNSEnter
from lib.py import RtnlFamily


LINK_NETNSID = 100
LINK_NETNSID2 = 200


def test_event() -> None:
    with NetNS() as ns1, NetNS() as ns2:
        with NetNSEnter(str(ns2)):
            rtnl = RtnlFamily()

        rtnl.ntf_subscribe("rtnlgrp-link")

        ip(f"netns set {ns2} {LINK_NETNSID}", ns=str(ns1))
        ip(f"link add netns {ns1} link-netnsid {LINK_NETNSID} dummy1 type dummy")
        ip(f"link add netns {ns1} dummy2 type dummy", ns=str(ns2))

        ip("link del dummy1", ns=str(ns1))
        ip("link del dummy2", ns=str(ns1))

        time.sleep(1)
        rtnl.check_ntf()
        ksft_true(rtnl.async_msg_queue.empty(),
                  "Received unexpected link notification")


def test_event_all_nsid() -> None:
    """NETLINK_LISTEN_ALL_NSID notifications: local events must not
    carry nsid even with a self-referential mapping.  Remote events
    must carry the correct nsid."""

    with NetNS() as ns1, NetNS() as ns2:
        net1, net2 = str(ns1), str(ns2)

        with NetNSEnter(net1):
            rtnl = RtnlFamily()
        rtnl.ntf_listen_all_nsid()
        rtnl.ntf_subscribe("rtnlgrp-link")

        # Case 1: no nsid assigned, local event, no nsid expected.
        ip("link add dummy-lo type dummy", ns=net1)

        # Case 2: self-referential nsid, local event, still no nsid.
        ip(f"netns set {net1} {LINK_NETNSID}", ns=net1)
        ip("link add dummy-sr type dummy", ns=net1)

        # Case 3: remote event, nsid present.
        ip(f"netns set {net2} {LINK_NETNSID2}", ns=net1)
        ip("link add dummy-re type dummy", ns=net2)

        # Collect the three newlink events, ignoring unrelated noise.
        events = {}
        for msg in rtnl.poll_ntf(duration=1):
            if msg['name'] == 'getlink':
                ifname = msg['msg'].get('ifname')
                if ifname in ('dummy-lo', 'dummy-sr', 'dummy-re'):
                    events[ifname] = msg
            if len(events) == 3:
                break

        ksft_true('dummy-lo' in events, "missing local event")
        ksft_true(events['dummy-lo'].get('nsid') is None,
                  "local event without nsid should not carry nsid")

        ksft_true('dummy-sr' in events, "missing self-ref event")
        ksft_true(events['dummy-sr'].get('nsid') is None,
                  "local event with self-ref nsid should not carry nsid")

        ksft_true('dummy-re' in events, "missing remote event")
        ksft_eq(events['dummy-re'].get('nsid'), LINK_NETNSID2,
                "remote event should carry nsid")

        ip("link del dummy-lo", ns=net1)
        ip("link del dummy-sr", ns=net1)
        ip("link del dummy-re", ns=net2)


def validate_link_netns(netns, ifname, link_netnsid) -> bool:
    link_info = ip(f"-d link show dev {ifname}", ns=netns, json=True)
    if not link_info:
        return False
    return link_info[0].get("link_netnsid") == link_netnsid


def test_link_net() -> None:
    configs = [
        # type, common args, type args, fallback to dev_net
        ("ipvlan", "link dummy1", "", False),
        ("macsec", "link dummy1", "", False),
        ("macvlan", "link dummy1", "", False),
        ("macvtap", "link dummy1", "", False),
        ("vlan", "link dummy1", "id 100", False),
        ("gre", "", "local 192.0.2.1", True),
        ("vti", "", "local 192.0.2.1", True),
        ("ipip", "", "local 192.0.2.1", True),
        ("ip6gre", "", "local 2001:db8::1", True),
        ("ip6tnl", "", "local 2001:db8::1", True),
        ("vti6", "", "local 2001:db8::1", True),
        ("sit", "", "local 192.0.2.1", True),
        ("xfrm", "", "if_id 1", True),
    ]

    with NetNS() as ns1, NetNS() as ns2, NetNS() as ns3:
        net1, net2, net3 = str(ns1), str(ns2), str(ns3)

        # prepare link netnsid  and a dummy link needed by certain drivers
        ip(f"netns set {net3} {LINK_NETNSID}", ns=str(net2))
        ip("link add dummy1 type dummy", ns=net3)

        cases = [
            # source, "netns", "link-netns", expected link-netns
            (net3, None, None, None, None),
            (net3, net2, None, None, LINK_NETNSID),
            (net2, None, net3, LINK_NETNSID, LINK_NETNSID),
            (net1, net2, net3, LINK_NETNSID, LINK_NETNSID),
        ]

        for src_net, netns, link_netns, exp1, exp2 in cases:
            tgt_net = netns or src_net
            for typ, cargs, targs, fb_dev_net in configs:
                cmd = "link add"
                if netns:
                    cmd += f" netns {netns}"
                if link_netns:
                    cmd += f" link-netns {link_netns}"
                cmd += f" {cargs} foo type {typ} {targs}"
                ip(cmd, ns=src_net)
                if fb_dev_net:
                    ksft_true(validate_link_netns(tgt_net, "foo", exp1),
                              f"{typ} link_netns validation failed")
                else:
                    ksft_true(validate_link_netns(tgt_net, "foo", exp2),
                              f"{typ} link_netns validation failed")
                ip(f"link del foo", ns=tgt_net)


def test_peer_net() -> None:
    types = [
        "vxcan",
        "netkit",
        "veth",
    ]

    with NetNS() as ns1, NetNS() as ns2, NetNS() as ns3, NetNS() as ns4:
        net1, net2, net3, net4 = str(ns1), str(ns2), str(ns3), str(ns4)

        ip(f"netns set {net3} {LINK_NETNSID}", ns=str(net2))

        cases = [
            # source, "netns", "link-netns", "peer netns", expected
            (net1, None, None, None, None),
            (net1, net2, None, None, None),
            (net2, None, net3, None, LINK_NETNSID),
            (net1, net2, net3, None, None),
            (net2, None, None, net3, LINK_NETNSID),
            (net1, net2, None, net3, LINK_NETNSID),
            (net2, None, net2, net3, LINK_NETNSID),
            (net1, net2, net4, net3, LINK_NETNSID),
        ]

        for src_net, netns, link_netns, peer_netns, exp in cases:
            tgt_net = netns or src_net
            for typ in types:
                cmd = "link add"
                if netns:
                    cmd += f" netns {netns}"
                if link_netns:
                    cmd += f" link-netns {link_netns}"
                cmd += f" foo type {typ}"
                if peer_netns:
                    cmd += f" peer netns {peer_netns}"
                ip(cmd, ns=src_net)
                ksft_true(validate_link_netns(tgt_net, "foo", exp),
                          f"{typ} peer_netns validation failed")
                ip(f"link del foo", ns=tgt_net)


def main() -> None:
    ksft_run([
        test_event,
        test_event_all_nsid,
        test_link_net,
        test_peer_net,
    ])
    ksft_exit()


if __name__ == "__main__":
    main()
