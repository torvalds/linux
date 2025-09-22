#!/usr/local/bin/python3

print("fully fragmented maximum size ping6 packet, sent in random order")

#          |----|
#                                        |----|
#                              |----|
#                                                       |----|
#     |----|

import os
import random
from addr import *
from scapy.all import *

pid=os.getpid()
eid=pid & 0xffff
iplen=2**16
size=424
payload=b"ABCDEFGHIJKLMNOP" * int(iplen / 16)
packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/ \
    ICMPv6EchoRequest(id=eid, data=bytes(payload)[0:iplen-8-1])
frag=[]
fid=pid & 0xffffffff
max=int(iplen/size)
for i in range(max):
	frag.append(IPv6ExtHdrFragment(nh=58, id=fid, m=1,
	    offset=i*int(size/8))/bytes(packet)[40+i*size:40+(i+1)*size])
frag.append(IPv6ExtHdrFragment(nh=58, id=fid,
    offset=max*int(size/8))/bytes(packet)[40+max*size:])
eth=[]
for f in frag:
	pkt=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/f
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/pkt)

child = os.fork()
if child == 0:
	time.sleep(1)
	randeth=eth
	random.shuffle(randeth)
	for e in randeth:
		sendp(e, iface=LOCAL_IF)
		time.sleep(0.001)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=10, filter=
    "ip6 and src "+REMOTE_ADDR6+" and dst "+LOCAL_ADDR6+" and proto ipv6-frag")
os.kill(child, 15)
os.wait()

for a in ans:
	if a and a.type == ETH_P_IPV6 and \
	    ipv6nh[a.payload.nh] == 'Fragment Header' and \
	    a.payload.payload.offset == 0 and \
	    ipv6nh[a.payload.payload.nh] == 'ICMPv6' and \
	    icmp6types[a.payload.payload.payload.type] == 'Echo Reply':
		id=a.payload.payload.payload.id
		print("id=%#x" % (id))
		if id != eid:
			print("WRONG ECHO REPLY ID")
			exit(2)
		exit(0)
print("NO ECHO REPLY")
exit(1)
