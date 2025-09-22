#!/usr/local/bin/python3

print("fully fragmented maximum size ping packet, sent in random order")

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
packet=IP(src=LOCAL_ADDR, dst=REMOTE_ADDR)/ \
    ICMP(type='echo-request', id=eid)/bytes(payload)[0:iplen-20-8-1]
frag=[]
fid=pid & 0xffff
max=int((iplen-20)/size)
for i in range(max):
	frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
	    frag=i*int(size/8), flags='MF')/
	    bytes(packet)[20+i*size:20+(i+1)*size])
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    frag=max*int(size/8))/bytes(packet)[20+max*size:])
eth=[]
for f in frag:
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/f)

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
    "ip and src "+REMOTE_ADDR+" and dst "+LOCAL_ADDR+" and icmp")
os.kill(child, 15)
os.wait()

for a in ans:
	if a and a.type == ETH_P_IP and \
	    a.payload.frag == 0 and \
	    a.payload.proto == 1 and \
	    icmptypes[a.payload.payload.type] == 'echo-reply':
		id=a.payload.payload.id
		print("id=%#x" % (id))
		if id != eid:
			print("WRONG ECHO REPLY ID")
			exit(2)
		exit(0)
print("NO ECHO REPLY")
exit(1)
