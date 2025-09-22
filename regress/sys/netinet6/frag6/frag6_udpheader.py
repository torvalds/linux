#!/usr/local/bin/python3

print("udp fragments splitted after udp header")

# |----|
#      |--------|

import os
from addr import *
from scapy.all import *

pid=os.getpid()
uport=pid & 0xffff
# inetd ignores UDP packets from privileged port or nfs
if uport < 1024 or uport == 2049:
	uport+=1024
payload=b"ABCDEFGHIJKLMNOP"
packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/ \
    UDP(sport=uport, dport=7)/payload
frag=[]
fid=pid & 0xffffffff
frag.append(IPv6ExtHdrFragment(nh=17, id=fid, m=1)/bytes(packet)[40:48])
frag.append(IPv6ExtHdrFragment(nh=17, id=fid, offset=1)/bytes(packet)[48:64])
eth=[]
for f in frag:
	pkt=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/f
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/pkt)

if os.fork() == 0:
	time.sleep(1)
	sendp(eth, iface=LOCAL_IF)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=3, filter=
    "ip6 and src "+REMOTE_ADDR6+" and dst "+LOCAL_ADDR6+" and udp")
for a in ans:
	if a and a.type == ETH_P_IPV6 and \
	    ipv6nh[a.payload.nh] == 'UDP' and \
	    a.payload.payload.sport == 7:
		port=a.payload.payload.dport
		print("port=%d" % (port))
		if port != uport:
			print("WRONG UDP ECHO REPLY PORT")
			exit(2)
		data=a.payload.payload.load
		print("payload=%s" % (data))
		if data == payload:
			exit(0)
		print("PAYLOAD!=%s" % (payload))
		exit(1)
print("NO UDP ECHO REPLY")
exit(2)
