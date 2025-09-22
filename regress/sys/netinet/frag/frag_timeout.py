#!/usr/local/bin/python3

print("6 non-overlapping ping fragments in 75 seconds, timeout is 60")

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
packet=IP(src=LOCAL_ADDR, dst=REMOTE_ADDR)/ \
    ICMP(type='echo-request', id=eid)/payload
frag=[]
fid=pid & 0xffff
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    flags='MF')/bytes(packet)[20:28])
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    frag=1, flags='MF')/bytes(packet)[28:36])
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    frag=2, flags='MF')/bytes(packet)[36:44])
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    frag=3, flags='MF')/bytes(packet)[44:52])
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    frag=4, flags='MF')/bytes(packet)[52:60])
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    frag=5)/bytes(packet)[60:68])
eth=[]
for f in frag:
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/f)

if os.fork() == 0:
	time.sleep(1)
	for e in eth:
		sendp(e, iface=LOCAL_IF)
		time.sleep(15)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=90, filter=
    "ip and src "+REMOTE_ADDR+" and dst "+LOCAL_ADDR+" and icmp")
for a in ans:
	if a and a.type == ETH_P_IP and \
	    a.payload.proto == 1 and \
	    a.payload.frag == 0 and a.payload.flags == 0 and \
	    icmptypes[a.payload.payload.type] == 'echo-reply':
		id=a.payload.payload.id
		print("id=%#x" % (id))
		if id != eid:
			print("WRONG ECHO REPLY ID")
			exit(2)
		data=a.payload.payload.payload.load
		print("payload=%s" % (data))
		if data == payload:
			print("ECHO REPLY")
			exit(1)
		print("PAYLOAD!=%s" % (payload))
		exit(1)
print("no echo reply")
exit(0)
