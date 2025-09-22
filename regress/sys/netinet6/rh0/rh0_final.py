#!/usr/local/bin/python3
# send a ping6 packet with routing header type 0
# the address pointer is at the final destination
# we expect a parameter problem from header scanning

print("send ping6 packet with routing header type 0 to the final destination")

import os
from addr import *
from scapy.all import *

eid=os.getpid() & 0xffff
payload=b"ABCDEFGHIJKLMNOP"
packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/\
    IPv6ExtHdrRouting(addresses=[OTHER_FAKE1_ADDR6, OTHER_FAKE2_ADDR6], \
    segleft=0)/\
    ICMPv6EchoRequest(id=eid, data=payload)
eth=Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/packet

if os.fork() == 0:
	time.sleep(1)
	sendp(eth, iface=LOCAL_IF)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=3, filter=
    "ip6 and dst "+LOCAL_ADDR6+" and icmp6")
for a in ans:
	if a and a.type == ETH_P_IPV6 and \
	    ipv6nh[a.payload.nh] == 'ICMPv6' and \
	    icmp6types[a.payload.payload.type] == 'Parameter problem':
		pprob=a.payload.payload
		code=pprob.code
		print("code=%#d" % (code))
		if code != 0:
			print("WRONG PARAMETER PROBLEM CODE")
			exit(2)
		ptr=pprob.ptr
		print("ptr=%#d" % (ptr))
		if ptr != 42:
			print("WRONG PARAMETER PROBLEM POINTER")
			exit(2)
		exit(0)
print("NO ICMP6 PARAMETER PROBLEM")
exit(1)
