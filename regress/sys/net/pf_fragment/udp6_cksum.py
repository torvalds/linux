#!/usr/local/bin/python3
# check udp6 checksum in returned icmp packet

import os
from addr import *
from scapy.all import *

dstaddr=sys.argv[1]
uport=os.getpid() & 0xffff
# inetd ignores UDP packets from privileged port or nfs
if uport < 1024 or uport == 2049:
	uport+=1024
payload=b"a" * 1452
p=(Ether(src=SRC_MAC, dst=PF_MAC)/IPv6(src=SRC_OUT6, dst=dstaddr)/
    UDP(sport=uport, dport=9)/payload)
udpcksum=IPv6(bytes(p.payload)).payload.chksum
print("udpcksum=%#04x" % (udpcksum))
a=srp1(p, iface=SRC_IF, timeout=2)
if a and a.type == ETH_P_IPV6 and \
    ipv6nh[a.payload.nh] == 'ICMPv6' and \
    icmp6types[a.payload.payload.type] == 'Packet too big':
	outercksum=a.payload.payload.cksum
	print("outercksum=%#04x" % (outercksum))
	q=a.payload.payload.payload
	if ipv6nh[q.nh] == 'UDP':
		innercksum=q.payload.chksum
		print("innercksum=%#04x" % (innercksum))
		if innercksum == udpcksum:
			exit(0)
		print("INNERCKSUM!=UDPCKSUM")
		exit(1)
	print("NO INNER UDP PACKET")
	exit(2)
print("NO PACKET TOO BIG")
exit(2)
