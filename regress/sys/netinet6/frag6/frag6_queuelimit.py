#!/usr/local/bin/python3

print("drop too long fragment queue, reassemble less fragments")

# |----|
#      |----|
#           |----|
#                 ...                                           ...
#                                                                  |----|

import os
from addr import *
from scapy.all import *

pid=os.getpid()
eid=pid & 0xffff
payload=b"ABCDEFGHIJKLMNOP" * 70
frag=[]
fid=pid & 0xffffffff
# send packets with 65 and 64 fragments
for max in (64, 63):
	eid = ~eid & 0xffff
	packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/ \
	    ICMPv6EchoRequest(id=eid, data=payload)
	fid = ~fid & 0xffffffff
	for i in range(max):
		frag.append(IPv6ExtHdrFragment(nh=58, id=fid, m=1,
		    offset=i)/bytes(packet)[40+i*8:40+(i+1)*8])
	frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
	    offset=max)/bytes(packet)[40+max*8:])
eth=[]
for f in frag:
	pkt=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/f
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/pkt)

child = os.fork()
if child == 0:
	time.sleep(1)
	for e in eth:
		sendp(e, iface=LOCAL_IF)
		time.sleep(0.001)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=10, filter=
    "ip6 and src "+REMOTE_ADDR6+" and dst "+LOCAL_ADDR6+" and icmp6")
os.kill(child, 15)
os.wait()

reply=False
for a in ans:
	if a and a.type == ETH_P_IPV6 and \
	    ipv6nh[a.payload.nh] == 'ICMPv6' and \
	    icmp6types[a.payload.payload.type] == 'Echo Reply':
		id=a.payload.payload.id
		print("id=%#x" % (id))
		if id == ~eid & 0xffff:
			print("ECHO REPLY FROM 65 FRAGMENTS")
			exit(1)
		if id != eid:
			print("WRONG ECHO REPLY ID")
			exit(2)
		data=a.payload.payload.data
		print("payload=%s" % (data))
		if data != payload:
			print("PAYLOAD!=%s" % (payload))
			exit(2)
		reply=True
if not reply:
	print("NO ECHO REPLY FROM 64 FRAGMENTS")
	exit(1)
print("echo reply from 64 fragments")
exit(0)
