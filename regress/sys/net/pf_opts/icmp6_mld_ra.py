#!/usr/local/bin/python3

print("send icmp6 multicast listener discovery with router alert")

import os
import sys
from struct import pack
from addr import *
from scapy.all import *

if len(sys.argv) != 2:
	print("usage: icmp6_mld_ra.py Nn")
	exit(2)

N=sys.argv[1]
IF=eval("IF_"+N);
ADDR6=eval("ADDR6_"+N);

pid=os.getpid()
eid=pid & 0xffff
packet=IPv6(src=ADDR6, dst="ff02::1", hlim=1)/ \
    IPv6ExtHdrHopByHop(options=RouterAlert())/ \
    ICMPv6MLQuery()

# send() does not work for some reason, add the bpf loopback layer manually
bpf=pack('!I', 24) + bytes(packet)
sendp(bpf, iface=IF)
