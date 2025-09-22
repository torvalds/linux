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
fid=pid & 0xffff
# send packets with 65 and 64 fragments
for max in (64, 63):
	eid = ~eid & 0xffff
	packet=IP(src=LOCAL_ADDR, dst=REMOTE_ADDR)/ \
	    ICMP(type='echo-request', id=eid)/payload
	fid = ~fid & 0xffff
	for i in range(max):
		frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
		    frag=i, flags='MF')/bytes(packet)[20+i*8:20+(i+1)*8])
	frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
	    frag=max)/bytes(packet)[20+max*8:])
eth=[]
for f in frag:
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/f)

child = os.fork()
if child == 0:
	time.sleep(1)
	for e in eth:
		sendp(e, iface=LOCAL_IF)
		time.sleep(0.001)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=10, filter=
    "ip and src "+REMOTE_ADDR+" and dst "+LOCAL_ADDR+" and icmp")
os.kill(child, 15)
os.wait()

reply=False
for a in ans:
	if a and a.type == ETH_P_IP and \
	    a.payload.proto == 1 and \
	    a.payload.frag == 0 and a.payload.flags == 0 and \
	    icmptypes[a.payload.payload.type] == 'echo-reply':
		id=a.payload.payload.id
		print("id=%#x" % (id))
		if id == ~eid & 0xffff:
			print("ECHO REPLY FROM 65 FRAGMENTS")
			exit(1)
		if id != eid:
			print("WRONG ECHO REPLY ID")
			exit(2)
		data=a.payload.payload.payload.load
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
