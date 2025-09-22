#!/usr/local/bin/python3
# $OpenBSD: sendrecv.py,v 1.3 2020/12/25 13:47:43 bluhm Exp $

import os
from scapy.all import *
from struct import pack
import getopt, sys

def usage():
	print("sendrecv [-hi] [-c ckoff] [-r recvsz] [-s sendsz]")
	print("    -c ckoff   set checksum offset within payload")
	print("    -h         help, show usage")
	print("    -i         expect icmp6 error message as response")
	print("    -r recvsz  expected payload size")
	print("    -s sendsz  set payload size")
	exit(1)

opts, args = getopt.getopt(sys.argv[1:], "c:hir:s:")

ip = IPv6(src="::1", dst="::1", nh=255)

ckoff = None
icmp = False
recvsz = None
sendsz = None
for o, a in opts:
	if o == "-c":
		ckoff = int(a)
	elif o == "-i":
		icmp = True
	elif o == "-r":
		recvsz = int(a)
	elif o == "-s":
		sendsz = int(a)
	else:
		usage()

payload = b"";
if sendsz is not None:
	for i in range(sendsz):
		payload += pack('B', i)
	print("payload length is", len(payload))

if ckoff is not None:
	payload = payload[:ckoff] + pack("xx") + payload[ckoff+2:]
	cksum = in6_chksum(255, ip, payload)
	print("calculated checksum is", cksum)
	payload = payload[:ckoff] + pack("!H", cksum) + payload[ckoff+2:]

req=ip/payload
# As we are sending from ::1 to ::1 we sniff our own packet as answer.
# Add a filter that matches on the expected answer using the payload size.
if icmp:
	filter="icmp6"
	if recvsz is not None:
		filter += (" and len = %d" % (4 + 40 + 8 + 40 + recvsz))
else:
	filter="proto 255"
	if recvsz is not None:
		filter += (" and len = %d" % (4 + 40 + recvsz))
print("filter", filter)
ans=sr(req, iface="lo0", filter=filter, timeout=10)
print(ans)
res=ans[0][0][1]
res.show()

print("response protocol next header is", res.nh)
if icmp:
	if res.nh != 58:
		print("response wrong protocol, expected icmp6")
		exit(1)
	print("response icmp6 type is", res.payload.type)
	if res.payload.type != 4:
		print("response wrong icmp6 type, expected parameter problem")
		exit(1)
	exit(0)

if res.nh != 255:
	print("response with wrong protocol, expected 255, got")
	exit(1)

cksum = in6_chksum(255, res, res.payload.load)
print("received checksum is", cksum)
if ckoff is not None and cksum != 0:
	print("received invalid checksum", cksum)
	exit(1)

print("received payload length is", len(res.payload.load))
if recvsz is not None:
	if len(res.payload.load) != recvsz:
		print("wrong payload length, expected", recvsz)
		exit(1)

exit(0)
