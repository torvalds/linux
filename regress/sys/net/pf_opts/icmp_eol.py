#!/usr/local/bin/python3

print("send icmp with option end of list")

import os
import sys
from struct import pack
from addr import *
from scapy.all import *

if len(sys.argv) != 2:
	print("usage: icmp_eol.py Nn")
	exit(2)

N=sys.argv[1]
IF=eval("IF_"+N);
ADDR=eval("ADDR_"+N);

pid=os.getpid()
eid=pid & 0xffff
payload=b"ABCDEFGHIJKLMNOP"
packet=IP(src=ADDR, dst=ADDR, options=b"\001\000\001\001")/ \
    ICMP(type=6, id=eid)/payload

# send() does not work for some reason, add the bpf loopback layer manually
bpf=pack('!I', 2) + bytes(packet)
sendp(bpf, iface=IF)
