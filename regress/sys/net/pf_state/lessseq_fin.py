#!/usr/local/bin/python3
# send TCP fin packet that is not at the end of the sequence

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

payload=b"abcdefgh01234567"

print("Send data to advance sequence number.")
data=TCP(sport=syn.sport, dport=syn.dport, flags='AP',
    seq=2, ack=synack.seq+1, window=(2**16)-1)/payload[0:16]
sniffer = Sniff1();
sniffer.filter = "src %s and tcp port %u and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-ack" % (ip.dst, syn.dport, ip.src, syn.sport)
sniffer.start()
time.sleep(1)
send(ip/data)
sniffer.join(timeout=7)
data_ack = sniffer.packet
if data_ack is None:
	print("ERROR: no matching ACK packet for data received")
	exit(1)
if data_ack.seq != synack.seq+1 or data_ack.ack != 2 + 16:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d in gap ack" %
	    (synack.seq+1, 2+16, data_ack.seq, data_ack.ack))
	exit(1)

print("Send fin within the sequence of data.")
halfseq_fin=TCP(sport=syn.sport, dport=syn.dport, flags='AF',
    seq=2+8, ack=synack.seq+1, window=(2**16)-1)
sniffer = Sniff1();
sniffer.filter = "src %s and tcp port %u and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-ack" % (ip.dst, syn.dport, ip.src, syn.sport)
sniffer.start()
time.sleep(1)
send(ip/halfseq_fin)
sniffer.join(timeout=7)
data_ack = sniffer.packet

# XXX nothing wrong here, check that fin was not accepted.

print("Send reset to cleanup the connection")
new_rst=TCP(sport=synack.dport, dport=synack.sport, flags='RA',
    seq=data_ack.ack, ack=data_ack.seq)
send(ip/new_rst)

exit(0)
