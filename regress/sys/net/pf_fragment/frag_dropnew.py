#!/usr/local/bin/python3
# old fragment completely overlaps new one

#      |------------|
#          |XXXX|
# |----|

# If the current fragment is completely overlapped by existing ones,
# drop the newer fragment.
#                 if (precut >= frent->fe_len)
#                         goto bad_fragment;
# 'Nearer' traffic wins.

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
payload=b"ABCDEFGHIJKLOMNOQRSTUVWX"
dummy=b"01234567"
packet=IP(src=SRC_OUT, dst=dstaddr)/ICMP(type='echo-request', id=eid)/payload
fid=pid & 0xffff
frag0=bytes(packet)[20:28]
frag1=dummy
frag2=bytes(packet)[28:52]
pkt0=IP(src=SRC_OUT, dst=dstaddr, proto=1, id=fid, frag=0, flags='MF')/frag0
pkt1=IP(src=SRC_OUT, dst=dstaddr, proto=1, id=fid, frag=2, flags='MF')/frag1
pkt2=IP(src=SRC_OUT, dst=dstaddr, proto=1, id=fid, frag=1)/frag2
eth=[]
eth.append(Ether(src=SRC_MAC, dst=PF_MAC)/pkt2)
eth.append(Ether(src=SRC_MAC, dst=PF_MAC)/pkt1)
eth.append(Ether(src=SRC_MAC, dst=PF_MAC)/pkt0)

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
