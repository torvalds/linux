#!/usr/local/bin/python3
# start of new fragment overlaps old one

# |--------|
#     |>>>>>----|

# If the tail of an older fragment overlaps the beginning of the
# current fragment, cut the newer fragment.
#                 m_adj(frent->fe_m, precut);
# Newer data wins.

import os
import threading
from addr import *
from scapy.all import *

class Sniff1(threading.Thread):
	filter = None
	captured = None
	packet = None
	def run(self):
		self.captured = sniff(iface=SRC_IF, filter=self.filter,
		    count=1, timeout=3)
		if self.captured:
			self.packet = self.captured[0]

dstaddr=sys.argv[1]
pid=os.getpid()
eid=pid & 0xffff
payload=b"ABCDEFGHIJKLOMNO"
dummy=b"01234567"
packet=IP(src=SRC_OUT, dst=dstaddr)/ICMP(type='echo-request', id=eid)/payload
frag0=bytes(packet)[20:36]
frag1=dummy+bytes(packet)[36:44]
fid=pid & 0xffff
pkt0=IP(src=SRC_OUT, dst=dstaddr, proto=1, id=fid, frag=0, flags='MF')/frag0
pkt1=IP(src=SRC_OUT, dst=dstaddr, proto=1, id=fid, frag=1)/frag1
eth=[]
eth.append(Ether(src=SRC_MAC, dst=PF_MAC)/pkt0)
eth.append(Ether(src=SRC_MAC, dst=PF_MAC)/pkt1)

sniffer = Sniff1();
sniffer.filter = "ip and src %s and dst %s and icmp" % (dstaddr, SRC_OUT)
sniffer.start()
time.sleep(1)
sendp(eth, iface=SRC_IF)
sniffer.join(timeout=5)
a = sniffer.packet

if a and a.type == ETH_P_IP and \
    a.payload.proto == 1 and \
    a.payload.frag == 0 and a.payload.flags == 0 and \
    icmptypes[a.payload.payload.type] == 'echo-reply':
	id=a.payload.payload.id
	print("id=%#x" % (id))
	if id != eid:
		print("WRONG ECHO REPLY ID")
		exit(2)
	load=a.payload.payload.payload.load
	print("payload=%s" % (load))
	if load == payload:
		exit(0)
	print("PAYLOAD!=%s" % (payload))
	exit(1)
print("NO ECHO REPLY")
exit(2)
