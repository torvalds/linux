#!/usr/local/bin/python3

print("udp fragments with splitted payload")

# |--------|
#          |----|

import os
from addr import *
from scapy.all import *

pid=os.getpid()
uport=pid & 0xffff
# inetd ignores UDP packets from privileged port or nfs
if uport < 1024 or uport == 2049:
	uport+=1024
payload=b"ABCDEFGHIJKLMNOP"
packet=IP(src=LOCAL_ADDR, dst=REMOTE_ADDR)/ \
    UDP(sport=uport, dport=7)/payload
frag=[]
fid=pid & 0xffff
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=17, id=fid,
    flags='MF')/bytes(packet)[20:36])
frag.append(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR, proto=17, id=fid,
    frag=2)/bytes(packet)[36:44])
eth=[]
for f in frag:
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/f)

if os.fork() == 0:
	time.sleep(1)
	sendp(eth, iface=LOCAL_IF)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=3, filter=
    "ip and src "+REMOTE_ADDR+" and dst "+LOCAL_ADDR+" and udp")
for a in ans:
	if a and a.type == ETH_P_IP and \
	    a.payload.proto == 17 and \
	    a.payload.frag == 0 and a.payload.flags == 0 and \
	    a.payload.payload.sport == 7:
		port=a.payload.payload.dport
		print("port=%d" % (port))
		if port != uport:
			print("WRONG UDP ECHO REPLY PORT")
			exit(2)
		data=a.payload.payload.payload.load
		print("payload=%s" % (data))
		if data == payload:
			exit(0)
		print("PAYLOAD!=%s" % (payload))
		exit(1)
print("NO UDP ECHO REPLY")
exit(2)
