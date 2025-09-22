#!/usr/local/bin/python3
# send Address Resolution Protocol Request
# expect Address Resolution Protocol response and check all fields
# RFC 826  An Ethernet Address Resolution Protocol
# Packet Generation

import os
from addr import *
from scapy.all import *

arp=ARP(op='who-has', hwsrc=LOCAL_MAC, psrc=LOCAL_ADDR,
    hwdst="ff:ff:ff:ff:ff:ff", pdst=REMOTE_ADDR)
eth=Ether(src=LOCAL_MAC, dst="ff:ff:ff:ff:ff:ff")/arp

e=srp1(eth, iface=LOCAL_IF, timeout=2)

if e and e.type == ETH_P_ARP:
	a=e.payload
	if a.hwtype != ARPHDR_ETHER:
		print("HWTYPE=%#0.4x != ARPHDR_ETHER" % (a.hwtype))
		exit(1)
	if a.ptype != ETH_P_IP:
		print("PTYPE=%#0.4x != ETH_P_IP" % (a.ptype))
		exit(1)
	if a.hwlen != 6:
		print("HWLEN=%d != 6" % (a.hwlen))
		exit(1)
	if a.plen != 4:
		print("PLEN=%d != 4" % (a.plen))
		exit(1)
	if a.op != 2:
		print("OP=%s != is-at" % (a.op))
		exit(1)
	if a.hwsrc != REMOTE_MAC:
		print("HWLOCAL=%s != REMOTE_MAC" % (a.hwsrc))
		exit(1)
	if a.psrc != REMOTE_ADDR:
		print("PLOCAL=%s != REMOTE_ADDR" % (a.psrc))
		exit(1)
	if a.hwdst != LOCAL_MAC:
		print("HWREMOTE=%s != LOCAL_MAC" % (a.hwdst))
		exit(1)
	if a.pdst != LOCAL_ADDR:
		print("PREMOTE=%s != LOCAL_ADDR" % (a.pdst))
		exit(1)
	print("arp reply")
	exit(0)

print("NO ARP REPLY")
exit(2)
