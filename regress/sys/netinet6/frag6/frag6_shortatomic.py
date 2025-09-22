#!/usr/local/bin/python3

print("fragment with dest option and atomic fragment without protocol header")

# |-IP-|-Frag-|-ExtDest-|-ICMP6-|-pay|
# |-- atomic fragment --|
#                                    |load-|

import os
from addr import *
from scapy.all import *

pid=os.getpid()
eid=pid & 0xffff
payload=b"ABCDEFGHIJKLMNOP"
packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/IPv6ExtHdrDestOpt()/ \
    ICMPv6EchoRequest(id=eid, data=payload)
frag=[]
fid=pid & 0xffffffff
frag.append(IPv6ExtHdrFragment(nh=60, id=fid, m=1)/bytes(packet)[40:64])
frag.append(IPv6ExtHdrFragment(nh=60, id=fid)/bytes(packet)[40:48])
frag.append(IPv6ExtHdrFragment(nh=60, id=fid, offset=3)/bytes(packet)[64:72])
eth=[]
for f in frag:
	pkt=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/f
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/pkt)

if os.fork() == 0:
	time.sleep(1)
	sendp(eth, iface=LOCAL_IF)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=3, filter=
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
			exit(0)
		print("PAYLOAD!=%s" % (payload))
		exit(2)
print("NO ECHO REPLY")
exit(1)
