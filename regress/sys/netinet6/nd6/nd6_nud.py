#!/usr/local/bin/python3
# send Neighbor Unreachability Detection neighbor solicitation
# expect an neighbor advertisement answer and check it

print("send neighbor unreachability detection solicitation packet")

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

ip=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/ICMPv6ND_NS(tgt=REMOTE_ADDR6)
eth=Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/ip

if os.fork() == 0:
	time.sleep(1)
	sendp(eth, iface=LOCAL_IF)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=3, filter=
    "ip6 and src "+REMOTE_ADDR6+" and dst "+LOCAL_ADDR6+" and icmp6")
for a in ans:
	if a and a.type == ETH_P_IPV6 and \
	    ipv6nh[a.payload.nh] == 'ICMPv6' and \
	    icmp6types[a.payload.payload.type] == 'Neighbor Advertisement':
		tgt=a.payload.payload.tgt
		print("target=%s" % (tgt))
		if tgt == REMOTE_ADDR6:
			exit(0)
		print("TARGET!=%s" % (REMOTE_ADDR6))
		exit(1)
print("NO NEIGHBOR ADVERTISEMENT")
exit(2)
