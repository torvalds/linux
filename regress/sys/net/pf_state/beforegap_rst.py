#!/usr/local/bin/python3
# send TCP reset packet that matches the ack before a sequence gap

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

payload=b"abcdefgh01234567ABCDEFGH"

print("Send data after gap.")
gap_after=TCP(sport=syn.sport, dport=syn.dport, flags='AP',
    seq=2+16, ack=synack.seq+1, window=(2**16)-1)/payload[16:24]
sniffer = Sniff1();
sniffer.filter = "src %s and tcp port %u and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-ack" % (ip.dst, syn.dport, ip.src, syn.sport)
sniffer.start()
time.sleep(1)
send(ip/gap_after)
sniffer.join(timeout=7)
start_ack = sniffer.packet
if start_ack is None:
	print("ERROR: no matching ACK packet at start received")
	exit(1)
if start_ack.seq != synack.seq+1 or start_ack.ack != 2:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d in start ack" %
	    (synack.seq+1, 2, start_ack.seq, start_ack.ack))
	exit(1)

print("Send data before gap.")
gap_before=TCP(sport=syn.sport, dport=syn.dport, flags='AP',
    seq=2, ack=synack.seq+1, window=(2**16)-1)/payload[0:8]
sniffer = Sniff1();
sniffer.filter = "src %s and tcp port %u and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-ack" % (ip.dst, syn.dport, ip.src, syn.sport)
sniffer.start()
time.sleep(1)
send(ip/gap_before)
sniffer.join(timeout=7)
gap_ack = sniffer.packet
if gap_ack is None:
	print("ERROR: no matching ACK packet at gap received")
	exit(1)
if gap_ack.seq != synack.seq+1 or gap_ack.ack != 10:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d in gap ack" %
	    (synack.seq+1, 10, gap_ack.seq, gap_ack.ack))
	exit(1)

print("Send reset before gap, data after gap has not been acknowleged.")
gap_rst=TCP(sport=syn.sport, dport=syn.dport, flags='AR',
    seq=2+8, ack=synack.seq+1, window=(2**16)-1)
send(ip/gap_rst)

print("Send new SYN packet to see if state is gone, receive SYN+ACK")
new_syn=TCP(sport=tport, dport='discard', flags='S',
    seq=2**24+1, window=(2**16)-1)
new_synack=sr1(ip/new_syn, timeout=5)
if new_synack is None:
	print("ERROR: no new matching SYN+ACK packet received")
	exit(1)

print("Send reset to cleanup the new connection")
new_rst=TCP(sport=new_synack.dport, dport=new_synack.sport, flags='RA',
    seq=new_synack.ack, ack=new_synack.seq)
send(ip/new_rst)

exit(0)
