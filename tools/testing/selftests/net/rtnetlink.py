#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import socket
import struct
import time
from lib.py import bkg, ip, ksft_exit, ksft_run, ksft_eq, ksft_ge, ksft_true, KsftSkipEx
from lib.py import ksft_not_in, ksft_not_none
from lib.py import CmdExitFailure, NetNS, NetNSEnter, RtnlAddrFamily, RtnlRouteFamily
from lib.py import defer

IPV4_ALL_HOSTS_MULTICAST = b'\xe0\x00\x00\x01'
IPV4_TEST_MULTICAST = b'\xef\x01\x01\x01'
IPV6_TEST_MULTICAST = bytes.fromhex('ff020000000000000000000000000123')


def _users_for(rtnl: RtnlAddrFamily, family: int, grp: bytes, ifindex: int):
    """Return mc-users for grp on ifindex, or 0 if absent."""

    addrs = rtnl.getmulticast({"ifa-family": family}, dump=True)
    matches = [addr for addr in addrs
               if addr['multicast'] == grp and addr['ifa-index'] == ifindex]
    if not matches:
        return 0
    if 'mc-users' not in matches[0]:
        return None

    return matches[0]['mc-users']


def dump_mcaddr_check() -> None:
    """
    Verify IPv4 multicast addresses and their user counts in RTM_GETMULTICAST.
    """

    with NetNS() as ns:
        with NetNSEnter(str(ns)):
            ip("link set lo up")
            rtnl = RtnlAddrFamily()
            lo_idx = socket.if_nametoindex('lo')
            addresses = rtnl.getmulticast({"ifa-family": socket.AF_INET}, dump=True)

            all_host_multicasts = [
                addr for addr in addresses
                if addr['multicast'] == IPV4_ALL_HOSTS_MULTICAST
            ]

            ksft_ge(len(all_host_multicasts), 1,
                    "No interface found with the IPv4 all-hosts multicast address")

            mreq = IPV4_TEST_MULTICAST + socket.inet_aton('127.0.0.1')
            before = _users_for(rtnl, socket.AF_INET, IPV4_TEST_MULTICAST, lo_idx)
            if before is None:
                raise KsftSkipEx("kernel does not expose IFA_MC_USERS")

            s1 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                s1.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
                s2.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

                after_join = _users_for(rtnl, socket.AF_INET,
                                        IPV4_TEST_MULTICAST, lo_idx)
                if after_join is None:
                    raise KsftSkipEx("kernel does not expose IFA_MC_USERS")
                ksft_eq(after_join - before, 2,
                        f"users delta != 2 after two joins "
                        f"(before={before}, after={after_join})")
            finally:
                s1.close()
                s2.close()


def dump_mcaddr6_check() -> None:
    """
    Verify IPv6 multicast addresses and their user counts in RTM_GETMULTICAST.
    """

    with NetNS() as ns:
        with NetNSEnter(str(ns)):
            ip("link set lo up")
            rtnl = RtnlAddrFamily()
            lo_idx = socket.if_nametoindex('lo')
            before = _users_for(rtnl, socket.AF_INET6,
                                IPV6_TEST_MULTICAST, lo_idx)
            if before is None:
                raise KsftSkipEx("kernel does not expose IFA_MC_USERS for IPv6")

            mreq = IPV6_TEST_MULTICAST + struct.pack('=I', lo_idx)
            s1 = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            s2 = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            try:
                s1.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, mreq)
                s2.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_JOIN_GROUP, mreq)

                after_join = _users_for(rtnl, socket.AF_INET6,
                                        IPV6_TEST_MULTICAST, lo_idx)
                if after_join is None:
                    raise KsftSkipEx("kernel does not expose IFA_MC_USERS for IPv6")
                ksft_eq(after_join - before, 2,
                        f"IPv6 users delta != 2 after two joins "
                        f"(before={before}, after={after_join})")
            finally:
                s1.close()
                s2.close()


def ipv4_devconf_notify() -> None:
    """
    Configure an interface and set ipv4-devconf values through netlink
    to verify that the appropriate netlink notifications are being sent.
    """

    with NetNS() as ns:
        with NetNSEnter(str(ns)):
            ifname = "dummy1"
            ip(f"link add name {ifname} type dummy", ns=str(ns))

            with bkg("ip monitor", ns=str(ns)) as cmd_obj:
                time.sleep(1)
                try:
                    ip(f"link set dev {ifname} inet forwarding on")
                    ip(f"link set dev {ifname} inet proxy_arp on")
                    ip(f"link set dev {ifname} inet rp_filter 1")
                    ip(f"link set dev {ifname} inet ignore_routes_with_linkdown on")
                except CmdExitFailure:
                    raise KsftSkipEx("iproute2 does not support IPv4 devconf attributes")
                time.sleep(1)

    ksft_true(f"inet {ifname} ignore_routes_with_linkdown on" in cmd_obj.stdout,
              f"No 'ignore_routes_with_linkdown on' notificiation found for interface {ifname}")
    ksft_true(f"inet {ifname} rp_filter strict" in cmd_obj.stdout,
              f"No 'rp_filter strict' notificiation found for interface {ifname}")
    ksft_true(f"inet {ifname} proxy_neigh on" in cmd_obj.stdout,
              f"No 'proxy_neigh on' notificiation found for interface {ifname}")
    ksft_true(f"inet {ifname} forwarding on" in cmd_obj.stdout,
              f"No 'forwarding on' notificiation found for interface {ifname}")

def _rtnl_route_subscribe(ns):
    with NetNSEnter(str(ns)):
        rtnl = RtnlRouteFamily()
    defer(rtnl.close)
    rtnl.ntf_subscribe("rtnlgrp-ipv6-route")
    return rtnl


def _wait_route_ntf(rtnl, name, dst_len, dst=None, deadline=10):
    """Return the attrs of the first matching notification, None on timeout."""

    for msg in rtnl.poll_ntf(duration=deadline):
        if msg['name'] != name:
            continue
        attrs = msg['msg']
        if attrs['rtm-dst-len'] != dst_len:
            continue
        if dst is not None and attrs.get('dst') != dst:
            continue
        return attrs
    return None


def _collect_route_ntfs(rtnl, name, want, deadline=10):
    """Gather attrs of matching notifications, keyed by (dst_len, dst)."""

    seen = {}
    for msg in rtnl.poll_ntf(duration=deadline):
        if msg['name'] != name:
            continue
        attrs = msg['msg']
        key = (attrs['rtm-dst-len'], attrs.get('dst'))
        if key in want:
            seen[key] = attrs
            if len(seen) == len(want):
                break
    return seen


def _write_ipv6_sysctl(name, value):
    with open(f"/proc/sys/net/ipv6/{name}", "w") as f:
        f.write(f"{value}\n")


def ipv6_route_del_reason_expired() -> None:
    """An expired route reports RTA_DEL_REASON == expired."""

    with NetNS() as ns:
        rtnl = _rtnl_route_subscribe(ns)
        with NetNSEnter(str(ns)):
            _write_ipv6_sysctl("route/gc_interval", 2)
        ip("link add name dummy1 type dummy", ns=str(ns))
        ip("link set dev dummy1 up", ns=str(ns))
        ip("-6 route add 2001:db8:2::/64 dev dummy1 expires 2", ns=str(ns))

        attrs = _wait_route_ntf(rtnl, 'delroute-ntf', 64, '2001:db8:2::',
                                deadline=15)
        ksft_not_none(attrs, "no RTM_DELROUTE for the expired route")
        if attrs is not None:
            ksft_eq(attrs.get('del-reason'), 'expired')


def _send_ra(sock, ifindex, lifetime, rio=None, pio=None):
    """The kernel fills in the ICMPv6 checksum on raw ICMPv6 sockets."""

    # type, code, cksum, hop limit, flags, router lifetime,
    # reachable time, retrans timer
    ra = struct.pack('!BBHBBHII', 134, 0, 0, 64, 0, lifetime, 0, 0)
    if rio is not None:
        prefix, plen, rio_lifetime = rio
        # RFC 4191 route information option, /64 prefix (8 bytes)
        ra += struct.pack('!BBBBI', 24, 2, plen, 0, rio_lifetime)
        ra += socket.inet_pton(socket.AF_INET6, prefix)[:8]
    if pio is not None:
        prefix, plen, valid_lft = pio
        # RFC 4861 prefix information option, on-link only (L set, A clear)
        ra += struct.pack('!BBBBIII', 3, 4, plen, 0x80, valid_lft, 0, 0)
        ra += socket.inet_pton(socket.AF_INET6, prefix)
    sock.sendto(ra, ('ff02::1', 0, 0, ifindex))


def _ra_router_sock(ns_r, ifname):
    with NetNSEnter(str(ns_r)):
        sock = socket.socket(socket.AF_INET6, socket.SOCK_RAW,
                             socket.IPPROTO_ICMPV6)
        sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 255)
        defer(sock.close)
        return sock, socket.if_nametoindex(ifname)


def _ra_advertise_routes(rtnl, sock, ifindex, want, **ra_opts):
    """
    Sending fails with EADDRNOTAVAIL while the router's link-local
    address is still tentative. addrconf_dad_start() only queues
    addrconf_dad_work(), and IFA_F_TENTATIVE is cleared when that work
    item runs, so retry until it does.
    """

    seen = {}
    for _ in range(10):
        try:
            _send_ra(sock, ifindex, **ra_opts)
        except OSError:
            time.sleep(0.2)
            continue
        seen.update(_collect_route_ntfs(rtnl, 'newroute-ntf',
                                        want - set(seen.keys()), deadline=2))
        if len(seen) == len(want):
            break
    return seen


def ipv6_route_del_reason_ra_withdrawn() -> None:
    """
    Routes withdrawn by a zero-lifetime RA (router lifetime, RFC 4861
    PIO, RFC 4191 RIO) report RTA_DEL_REASON == ra-withdrawn.
    """

    # (rtm-dst-len, dst); the default route carries no RTA_DST
    routes = {(0, None), (64, '2001:db8:6::'), (64, '2001:db8:5::')}

    with NetNS() as ns_h, NetNS() as ns_r:
        ip(f"link add veth0 netns {ns_h} type veth peer name veth1 netns {ns_r}")
        with NetNSEnter(str(ns_h)):
            _write_ipv6_sysctl("conf/veth0/accept_ra", 2)
            _write_ipv6_sysctl("conf/veth0/forwarding", 0)
            try:
                _write_ipv6_sysctl("conf/veth0/accept_ra_rt_info_max_plen", 64)
            except FileNotFoundError:
                raise KsftSkipEx("no CONFIG_IPV6_ROUTE_INFO")
        with NetNSEnter(str(ns_r)):
            # skip the DAD probe so the router's link-local source only
            # has to wait for addrconf_dad_work() to clear IFA_F_TENTATIVE
            _write_ipv6_sysctl("conf/veth1/accept_dad", 0)
        ip("link set dev veth0 up", ns=str(ns_h))
        ip("link set dev veth1 up", ns=str(ns_r))

        rtnl = _rtnl_route_subscribe(ns_h)
        sock, ifindex = _ra_router_sock(ns_r, "veth1")

        seen = _ra_advertise_routes(rtnl, sock, ifindex, routes,
                                    lifetime=1800,
                                    rio=('2001:db8:5::', 64, 600),
                                    pio=('2001:db8:6::', 64, 600))
        ksft_eq(set(seen), routes, "not all RA routes were installed")
        if set(seen) != routes:
            return

        _send_ra(sock, ifindex, 0, rio=('2001:db8:5::', 64, 0),
                 pio=('2001:db8:6::', 64, 0))
        seen = _collect_route_ntfs(rtnl, 'delroute-ntf', routes)
        for key in routes:
            attrs = seen.get(key)
            ksft_not_none(attrs, f"no RTM_DELROUTE for {key}")
            if attrs is not None:
                ksft_eq(attrs.get('del-reason'), 'ra-withdrawn')


def ipv6_route_del_reason_absent() -> None:
    """
    A deletion path that records no cause (here a userspace request)
    must not carry RTA_DEL_REASON at all.
    """

    with NetNS() as ns:
        rtnl = _rtnl_route_subscribe(ns)
        ip("link add name dummy1 type dummy", ns=str(ns))
        ip("link set dev dummy1 up", ns=str(ns))
        ip("-6 route add 2001:db8:1::/64 dev dummy1", ns=str(ns))
        ip("-6 route del 2001:db8:1::/64 dev dummy1", ns=str(ns))

        attrs = _wait_route_ntf(rtnl, 'delroute-ntf', 64, '2001:db8:1::')
        ksft_not_none(attrs, "no RTM_DELROUTE for 2001:db8:1::/64")
        if attrs is not None:
            ksft_not_in('del-reason', attrs,
                        "user deletion must not carry del-reason")


def main() -> None:
    ksft_run([dump_mcaddr_check, dump_mcaddr6_check, ipv4_devconf_notify,
              ipv6_route_del_reason_expired,
              ipv6_route_del_reason_ra_withdrawn,
              ipv6_route_del_reason_absent])
    ksft_exit()

if __name__ == "__main__":
    main()
