#!/usr/local/bin/python3
# send a ping6 packet with routing header type 0
# the address list is empty
# hide the routing header in a second fragment to preclude header scan
# we expect an echo reply, as there are no more hops

print("send with fragment and routing header type 0 to be source routed")

import os
from addr import *
from scapy.all import *

pid=os.getpid()
eid=pid & 0xffff
payload=b"ABCDEFGHIJKLMNOP"
packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/\
    IPv6ExtHdrDestOpt()/\
    IPv6ExtHdrRouting(addresses=[])/\
    ICMPv6EchoRequest(id=eid, data=payload)
frag=[]
fid=pid & 0xffffffff
frag.append(IPv6ExtHdrFragment(nh=60, id=fid, m=1)/bytes(packet)[40:48])
frag.append(IPv6ExtHdrFragment(nh=60, id=fid, offset=1)/bytes(packet)[48:80])
eth=[]
for f in frag:
	pkt=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/f
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/pkt)

if os.fork() == 0:
	time.sleep(1)
	sendp(eth, iface=LOCAL_IF)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=3, filter=
    "ip6 and dst "+LOCAL_ADDR6+" and icmp6")
for a in ans:
	if a and a.type == ETH_P_IPV6 and \
	    ipv6nh[a.payload.nh] == 'ICMPv6' and \
	    icmp6types[a.payload.payload.type] == 'Echo Reply':
		reply=a.payload.payload
		id=reply.id
		print("id=%#x" % (id))
		if id != eid:
			print("WRONG ECHO REPLY ID")
			exit(2)
		data=reply.data
		print("payload=%s" % (data))
		if data != payload:
			print("WRONG PAYLOAD")
			exit(2)
		exit(0)
print("NO ICMP6 ECHO REPLY")
exit(1)
