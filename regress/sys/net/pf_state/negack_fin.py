#!/usr/local/bin/python3
# send TCP FIN packet that uses ack number that was never sent before

import os
import threading
from addr import *
from scapy.all import *

#
# we can not use scapy's sr() function as receive side
# ignores the packet we expect to see. Packet is ignored
# due to mismatching sequence numbers.
#
class Sniff1(threading.Thread):
	filter = None
	captured = None
	packet = None
	def run(self):
		self.captured = sniff(iface=LOCAL_IF, filter=self.filter,
		    count=1, timeout=5)
		if self.captured:
			self.packet = self.captured[0]

tport=os.getpid() & 0xffff

ip=IP(src=FAKE_NET_ADDR, dst=REMOTE_ADDR)

print("Send SYN packet, receive SYN+ACK")
syn=TCP(sport=tport, dport='discard', flags='S', seq=1, window=(2**16)-1)
synack=sr1(ip/syn, timeout=5)
if synack is None:
	print("ERROR: no matching SYN+ACK packet received")
	exit(1)

print("Send ACK packet to finish handshake.")
ack=TCP(sport=synack.dport, dport=synack.sport, flags='A',
    seq=2, ack=synack.seq+1)
send(ip/ack)

print("Send fin with ack number that was never sent.")
negack_fin=TCP(sport=syn.sport, dport=syn.dport, flags='AF',
    seq=2, ack=synack.seq+1 - 5, window=(2**16)-1)
sniffer = Sniff1();
sniffer.filter = "src %s and tcp port %u and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-ack" % (ip.dst, syn.dport, ip.src, syn.sport)
sniffer.start()
time.sleep(1)
send(ip/negack_fin)
sniffer.join(timeout=7)
negack_ack = sniffer.packet
if negack_ack is None:
	print("ERROR: no matching ACK packet for FIN with negative ack")
	exit(1)
if negack_ack.seq != synack.seq+1 or negack_ack.ack != 3:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d in gap ack" %
	    (synack.seq+1, 3, negack_ack.seq, negack_ack.ack))
	exit(1)

print("Send reset to cleanup the connection")
new_rst=TCP(sport=synack.dport, dport=synack.sport, flags='RA',
    seq=negack_ack.ack, ack=negack_ack.seq)
send(ip/new_rst)

exit(0)
