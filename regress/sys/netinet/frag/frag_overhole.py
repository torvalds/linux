#!/usr/local/bin/python3

print("ping fragment at index boundary which modifies pf hole counter")

#                               index boundary 4096 |
# |--------------|
#                 ....
#                     |--------------|
#                                    |----------|
#                                               |XXXX----------|
#                                          |XXXX----|
#                                                               |---|

# this should trigger "frag tail overlap %d" and "frag head overlap %d"

import os
from addr import *
from scapy.all import *

pid=os.getpid()
eid=pid & 0xffff
payload=b"ABCDEFGHIJKLMNOP"
dummy=b"01234567"
fragsize=1024
boundary=4096
fragnum=int(boundary/fragsize)
packet=IP(src=LOCAL_ADDR, dst=REMOTE_ADDR)/ \
    ICMP(type='echo-request', id=eid)/ \
    ((int((boundary+fragsize)/len(payload)) + 1) * payload)
packet_length=len(packet)
frag=[]
fid=pid & 0xffff
for i in range(fragnum-1):
	frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
	    frag=(i*fragsize)>>3, flags='MF')/
	    bytes(packet)[20+i*fragsize:20+(i+1)*fragsize])
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    frag=(boundary-fragsize)>>3, flags='MF')/
    bytes(packet)[20+boundary-fragsize:20+boundary-len(dummy)])
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    frag=(boundary-len(dummy))>>3, flags='MF')/
    (dummy+bytes(packet)[20+boundary:20+boundary+fragsize]))
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    frag=(boundary-8-len(dummy))>>3, flags='MF')/
    (dummy+bytes(packet)[20+boundary-8:20+boundary]))
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
    frag=(boundary+fragsize)>>3)/bytes(packet)[20+boundary+fragsize:])
eth=[]
for f in frag:
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/f)

if os.fork() == 0:
	time.sleep(1)
	for e in eth:
		sendp(e, iface=LOCAL_IF)
		time.sleep(0.001)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=3, filter=
    "ip and src "+REMOTE_ADDR+" and dst "+LOCAL_ADDR+" and icmp")
for a in ans:
	if a and a.type == ETH_P_IP and \
	    a.payload.proto == 1 and \
	    a.payload.frag == 0 and \
	    icmptypes[a.payload.payload.type] == 'echo-reply':
		id=a.payload.payload.id
		print("id=%#x" % (id))
		if id != eid:
			print("WRONG ECHO REPLY ID")
			exit(2)
	if a and a.type == ETH_P_IP and \
	    a.payload.proto == 1 and \
	    a.payload.frag > 0 and \
	    a.payload.flags == '':
		len=(a.payload.frag<<3)+a.payload.len
		print("len=%d" % (len))
		if len != packet_length:
			print("WRONG ECHO REPLY LENGTH")
			exit(1)
		exit(0)
print("NO ECHO REPLY")
exit(1)
