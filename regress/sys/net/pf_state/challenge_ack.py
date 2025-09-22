#!/usr/local/bin/python3
# check wether path mtu to dst is as expected

import os
import threading
from addr import *
from scapy.all import *

#
# we can not use scapy's sr() function as receive side
# ignores the packet we expect to see. Packet is ignored
# due to mismatching sequence numbers. 'bogus_syn' is using
# seq = 1000000, while response sent back by PF has ack,
# which fits regular session opened by 'syn'.
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
syn=TCP(sport=tport, dport='echo', seq=1, flags='S', window=(2**16)-1)
synack=sr1(ip/syn, timeout=5)

if synack is None:
	print("ERROR: no matching SYN+ACK packet received")
	exit(1)

print("Send ACK packet to finish handshake.")
ack=TCP(sport=synack.dport, dport=synack.sport, seq=2, flags='A',
    ack=synack.seq+1)
send(ip/ack)

print("Connection is established, send bogus SYN, expect challenge ACK")
bogus_syn=TCP(sport=syn.sport, dport=syn.dport, seq=1000000, flags='S',
    window=(2**16)-1)
sniffer = Sniff1();
sniffer.filter = "src %s and tcp port %u and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-ack" % (ip.dst, syn.dport, ip.src, syn.sport)
sniffer.start()
time.sleep(1)
send(ip/bogus_syn)
sniffer.join(timeout=7)
challenge_ack = sniffer.packet

if challenge_ack is None:
	print("ERROR: no matching ACK packet received")
	exit(1)

if challenge_ack.seq != synack.seq+1:
	print("ERROR: expecting seq %d got %d in challange ack" % \
	    (challenge_ack.seq, synack.seq+1))
	exit(1)

print("Send reset to cleanup the connection")
new_rst=TCP(sport=synack.dport, dport=synack.sport, flags='RA',
    seq=synack.ack, ack=synack.seq)
send(ip/new_rst)

exit(0)
