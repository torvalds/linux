#!/usr/local/bin/python3
# check ip and udp checksum in returned icmp packet

import os
from addr import *
from scapy.all import *

dstaddr=sys.argv[1]
uport=os.getpid() & 0xffff
# inetd ignores UDP packets from privileged port or nfs
if uport < 1024 or uport == 2049:
	uport+=1024
payload=b"a" * 1472
p=(Ether(src=SRC_MAC, dst=PF_MAC)/IP(flags="DF", src=SRC_OUT, dst=dstaddr)/
    UDP(sport=uport, dport=9)/payload)
ipcksum=IP(bytes(p.payload)).chksum
print("ipcksum=%#04x" % (ipcksum))
udpcksum=IP(bytes(p.payload)).payload.chksum
print("udpcksum=%#04x" % (udpcksum))
a=srp1(p, iface=SRC_IF, timeout=2)
if a and a.type == ETH_P_IP and \
    a.payload.proto == 1 and \
    icmptypes[a.payload.payload.type] == 'dest-unreach' and \
    icmpcodes[a.payload.payload.type][a.payload.payload.code] == \
    'fragmentation-needed':
	outeripcksum=a.payload.chksum
	print("outeripcksum=%#04x" % (outeripcksum))
	outercksum=a.payload.payload.chksum
	print("outercksum=%#04x" % (outercksum))
	q=a.payload.payload.payload
	inneripcksum=q.chksum
	print("inneripcksum=%#04x" % (inneripcksum))
	if q.proto == 17:
		innercksum=q.payload.chksum
		print("innercksum=%#04x" % (innercksum))
		if innercksum == udpcksum:
			exit(0)
		print("INNERCKSUM!=UDPCKSUM")
		exit(1)
	print("NO INNER UDP REQUEST")
	exit(2)
print("NO FRAGMENTATION NEEDED")
exit(2)
