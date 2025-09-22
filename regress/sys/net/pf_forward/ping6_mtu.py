#!/usr/local/bin/python3
# check wether path mtu to dst is as expected

import os
import threading
from addr import *
from scapy.all import *

# usage: ping6_mtu src dst size icmp6-size

# work around the broken sniffing of packages with bad checksum
#a=srp1(eth, iface=SRC_IF, timeout=2)
class Sniff1(threading.Thread):
	filter = None
	captured = None
	packet = None
	def run(self):
		self.captured = sniff(iface=SRC_IF, filter=self.filter,
		    count=1, timeout=3)
		if self.captured:
			self.packet = self.captured[0]

srcaddr=sys.argv[1]
dstaddr=sys.argv[2]
size=int(sys.argv[3])
expect=int(sys.argv[4])
eid=os.getpid() & 0xffff
hdr=IPv6(src=srcaddr, dst=dstaddr)/ICMPv6EchoRequest(id=eid)
payload="a" * (size - len(bytes(hdr)))
ip=hdr/payload
iplen=IPv6(bytes(ip)).plen
eth=Ether(src=SRC_MAC, dst=PF_MAC)/ip

sniffer = Sniff1();
# pcap cannot access icmp6, check for packet too big, avoid neighbor discovery
sniffer.filter = "ip6 and dst %s and icmp6 and ip6[40] = 2 and ip6[41] = 0" \
    % srcaddr
sniffer.start()
time.sleep(1)
sendp(eth, iface=SRC_IF)
sniffer.join(timeout=5)
a = sniffer.packet

if a is None:
	print("no packet sniffed")
	exit(2)
if a and a.type == ETH_P_IPV6 and \
    ipv6nh[a.payload.nh] == 'ICMPv6' and \
    icmp6types[a.payload.payload.type] == 'Packet too big':
	mtu=a.payload.payload.mtu
	print("mtu=%d" % (mtu))
	if mtu != expect:
		print("MTU!=%d" % (expect))
		exit(1)
	iip=a.payload.payload.payload
	iiplen=iip.plen
	if iiplen != iplen:
		print("inner IPv6 plen %d!=%d" % (iiplen, iplen))
		exit(1)
	isrc=iip.src
	if isrc != srcaddr:
		print("inner IPv6 src %d!=%d" % (isrc, srcaddr))
		exit(1)
	idst=iip.dst
	if idst != dstaddr:
		print("inner IPv6 dst %d!=%d" % (idst, dstaddr))
		exit(1)
	exit(0)
print("MTU=UNKNOWN")
exit(2)
