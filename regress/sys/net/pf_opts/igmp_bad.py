#!/usr/local/bin/python3

print("send internet group management protocol with unknown option")

import os
import sys
from struct import pack
from addr import *
from scapy.all import *
from scapy.contrib.igmp import *

if len(sys.argv) != 2:
	print("usage: igmp_bad.py Nn")
	exit(2)

N=sys.argv[1]
IF=eval("IF_"+N);
ADDR=eval("ADDR_"+N);

pid=os.getpid()
eid=pid & 0xffff
packet=IP(src=ADDR, dst="224.0.0.1", ttl=1, options=b"\003\004\000\000")/ \
    IGMP(type=0x11)

# send() does not work for some reason, add the bpf loopback layer manually
bpf=pack('!I', 2) + bytes(packet)
sendp(bpf, iface=IF)
