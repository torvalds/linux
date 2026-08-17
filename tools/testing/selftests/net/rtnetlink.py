#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

import socket
import time
from lib.py import bkg, ip, ksft_exit, ksft_run, ksft_ge, ksft_true, KsftSkipEx
from lib.py import CmdExitFailure, NetNS, NetNSEnter, RtnlAddrFamily

IPV4_ALL_HOSTS_MULTICAST = b'\xe0\x00\x00\x01'

def dump_mcaddr_check() -> None:
    """
    Verify that at least one interface has the IPv4 all-hosts multicast address.
    At least the loopback interface should have this address.
    """

    rtnl = RtnlAddrFamily()
    addresses = rtnl.getmulticast({"ifa-family": socket.AF_INET}, dump=True)

    all_host_multicasts = [
        addr for addr in addresses if addr['multicast'] == IPV4_ALL_HOSTS_MULTICAST
    ]

    ksft_ge(len(all_host_multicasts), 1,
            "No interface found with the IPv4 all-hosts multicast address")

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

def main() -> None:
    ksft_run([dump_mcaddr_check, ipv4_devconf_notify])
    ksft_exit()

if __name__ == "__main__":
    main()
