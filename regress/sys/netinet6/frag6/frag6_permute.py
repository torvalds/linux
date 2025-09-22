#!/usr/local/bin/python3

print("send 3 non-overlapping ping6 fragments in all possible orders")

# |----|
#      |----|
#           |----|

import os
from addr import *
from scapy.all import *

permute=[]
permute.append([0,1,2])
permute.append([0,2,1])
permute.append([1,0,2])
permute.append([2,0,1])
permute.append([1,2,0])
permute.append([2,1,0])

pid=os.getpid()
payload=b"ABCDEFGHIJKLMNOP"
for p in permute:
	pid += 1
	eid=pid & 0xffff
	packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/ \
	    ICMPv6EchoRequest(id=eid, data=payload)
	frag=[]
	fid=pid & 0xffffffff
	frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
	    m=1)/bytes(packet)[40:48])
	frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
	    offset=1, m=1)/bytes(packet)[48:56])
	frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
	    offset=2)/bytes(packet)[56:64])
	eth=[]
	for i in range(3):
		pkt=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/frag[p[i]]
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
				break
			print("PAYLOAD!=%s" % (payload))
			exit(1)
	else:
		print("NO ECHO REPLY")
		exit(2)
print("permutation done")
exit(0)
