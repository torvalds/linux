#!/usr/local/bin/python3

print("ping6 fragments with options total larger than IP maximum packet")

#           |--------|
#                     ...                                  ...
#                                                             |--------|
# drop first fragment with ECN conflict, reinsert with longer unfrag part
# |---------|
# HopByHop|---------|

import os
from addr import *
from scapy.all import *

pid=os.getpid()
eid=pid & 0xffff
payload=b"ABCDEFGHIJKLMNOP"
packet=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/ \
    ICMPv6EchoRequest(id=eid, data=4095*payload)
plen=IPv6(raw(packet)).plen
print("plen=%u" % (plen))
if plen != 0xfff8:
	print("PLEN!=%u" % (0xfff8))
	exit(2)
bytes=bytes(packet)

frag=[]
fid=pid & 0xffffffff
off=2**7
while off < 2**13:
	frag.append(IPv6ExtHdrFragment(nh=58, id=fid, offset=off)/ \
	    bytes[40+off*8:40+off*8+2**10])
	off+=2**7
eth=[]
for f in frag:
	pkt=IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/f
	eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/pkt)

# first fragment with ECN to be dropped
eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/
    IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6, tc=3)/
    IPv6ExtHdrFragment(nh=58, id=fid, m=1)/bytes[40:40+2**10])

# resend first fragment with unfragmentable part too long for IP plen
eth.append(Ether(src=LOCAL_MAC, dst=REMOTE_MAC)/
    IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/
    IPv6ExtHdrHopByHop(options=PadN(optdata="\0"*4))/
    IPv6ExtHdrFragment(nh=58, id=fid, m=1)/bytes[40:40+2**10])

if os.fork() == 0:
	time.sleep(1)
	sendp(eth, iface=LOCAL_IF)
	os._exit(0)

ans=sniff(iface=LOCAL_IF, timeout=3, filter=
    "ip6 and src "+REMOTE_ADDR6+" and dst "+LOCAL_ADDR6+" and icmp6")
for a in ans:
	print("type %d" % (a.payload.payload.type))
	print("icmp %s" % (icmp6types[a.payload.payload.type]))
	if a and a.type == ETH_P_IPV6 and \
	    ipv6nh[a.payload.nh] == 'ICMPv6' and \
	    icmp6types[a.payload.payload.type] == 'Parameter problem':
		print("code=%u" % (a.payload.payload.code))
		# 0: 'erroneous header field encountered'
		if a.payload.payload.code != 0:
			print("WRONG PARAMETER PROBLEM CODE")
			exit(1)
		ptr=a.payload.payload.ptr
		print("ptr=%u" % (ptr))
		# 42: sizeof IPv6 header + offset in fragment header
		if ptr != 42:
			print("PTR!=%u" % (ptr))
			exit(1)
		exit(0)
print("NO ICMP PARAMETER PROBLEM")
exit(2)
