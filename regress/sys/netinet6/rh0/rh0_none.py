#!/usr/local/bin/python3
# send a ping6 packet without routing header type 0
# we expect an echo reply, as there is no routing header

print("send ping6 packet without routing header type 0")

import os
from addr import *
from scapy.all import *

eid=os.getpid() & 0xffff
payload=b"ABCDEFGHIJKLMNOP"
packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/\
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
