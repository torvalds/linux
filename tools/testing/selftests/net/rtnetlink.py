#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from lib.py import ksft_exit, ksft_run, ksft_ge, RtnlAddrFamily
import socket

IPV4_ALL_HOSTS_MULTICAST = b'\xe0\x00\x00\x01'

def dump_mcaddr_check(rtnl: RtnlAddrFamily) -> None:
    """
    Verify that at least one interface has the IPv4 all-hosts multicast address.
    At least the loopback interface should have this address.
    """

    addresses = rtnl.getmulticast({"ifa-family": socket.AF_INET}, dump=True)

    all_host_multicasts = [
        addr for addr in addresses if addr['ifa-multicast'] == IPV4_ALL_HOSTS_MULTICAST
    ]

    ksft_ge(len(all_host_multicasts), 1,
            "No interface found with the IPv4 all-hosts multicast address")

def main() -> None:
    rtnl = RtnlAddrFamily()
    ksft_run([dump_mcaddr_check], args=(rtnl, ))
    ksft_exit()

if __name__ == "__main__":
    main()
