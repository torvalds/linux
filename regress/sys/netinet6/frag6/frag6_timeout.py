#!/usr/local/bin/python3

print("6 non-overlapping ping6 fragments in 75 seconds, timeout is 60")

# |----|
#      |----|
#           |----|
#                |----|
#                     |----|      <--- timeout
#                          |----|

import os
from addr import *
from scapy.all import *

pid=os.getpid()
eid=pid & 0xffff
payload=b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcd"
packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/ \
    ICMPv6EchoRequest(id=eid, data=payload)
frag=[]
fid=pid & 0xffffffff
frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
    m=1)/bytes(packet)[40:48])
frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
    offset=1, m=1)/bytes(packet)[48:56])
frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
    offset=2, m=1)/bytes(packet)[56:64])
frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
    offset=3, m=1)/bytes(packet)[64:72])
frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
    offset=4, m=1)/bytes(packet)[72:80])
frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
    offset=5)/bytes(packet)[80:88])
eth=[]
for f in frag:
	pkt=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/f
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/pkt)

if os.fork() == 0:
	time.sleep(1)
	for e in eth:
		sendp(e, iface=LOCAL_IF)
		time.sleep(15)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=90, filter=
    "ip6 and src "+REMOTE_ADDR6+" and dst "+LOCAL_ADDR6+" and icmp6")
for a in ans:
	if a and a.type == ETH_P_IPV6 and \
	    ipv6nh[a.payload.nh] == 'ICMPv6' and \
	    icmp6types[a.payload.payload.type] == 'Echo Reply':
		id=a.payload.payload.id
		print("id=%#x" % (id))
		if id != eid:
			print("WRONG ECHO REPLY ID")
			exit(2)
		data=a.payload.payload.data
		print("payload=%s" % (data))
		if data == payload:
			print("ECHO REPLY")
			exit(1)
		print("PAYLOAD!=%s" % (payload))
		exit(2)
print("no echo reply")
exit(0)
