#!/usr/local/bin/python3
# send Unsolicited Neighbor Advertisement

print("send unsolicited neighbor advertisement packet")

import os
from addr import *
from scapy.all import *

# link-local solicited-node multicast address
def nsma(a):
	n = inet_pton(socket.AF_INET6, a)
	return inet_ntop(socket.AF_INET6, in6_getnsma(n))

# ethernet multicast address of multicast address
def nsmac(a):
	n = inet_pton(socket.AF_INET6, a)
	return in6_getnsmac(n)

# ethernet multicast address of solicited-node multicast address
def nsmamac(a):
	return nsmac(nsma(a))

# link-local address
def lla(m):
	return "fe80::"+in6_mactoifaceid(m)

ip=IPv6(src=lla(LOCAL_MAC), dst="ff02::1")/ICMPv6ND_NA(tgt=LOCAL_ADDR6)
eth=Ether(src=LOCAL_MAC, dst=nsmac("ff02::1"))/ip

sendp(eth, iface=LOCAL_IF)
time.sleep(1)

exit(0)
