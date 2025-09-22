#!/usr/local/bin/python3

print("send 3 non-overlapping ping fragments in all possible orders")

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
	packet=IP(src=LOCAL_ADDR, dst=REMOTE_ADDR)/ \
	    ICMP(type='echo-request', id=eid)/payload
	frag=[]
	fid=pid & 0xffff
	frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
	    flags='MF')/bytes(packet)[20:28])
	frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
	    frag=1, flags='MF')/bytes(packet)[28:36])
	frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=1, id=fid,
	    frag=2)/bytes(packet)[36:48])
	eth=[]
	for i in range(3):
		eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/frag[p[i]])

	if os.fork() == 0:
		time.sleep(1)
		sendp(eth, iface=LOCAL_IF)
		os._exit(0)

	ans=sniff(iface=LOCAL_IF, timeout=3, filter=
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
				break
			print("PAYLOAD!=%s" % (payload))
			exit(1)
	else:
		print("NO ECHO REPLY")
		exit(2)
print("permutation done")
exit(0)
