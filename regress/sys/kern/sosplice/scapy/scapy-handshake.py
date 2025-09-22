#!/usr/local/bin/python3
# do a tcp handshake for the client and server connection

import os
import sys
import threading
from addr import *
from scapy.all import *

client=os.getpid() & 0xffff
relay=int(sys.argv[2])
server=int(sys.argv[1])

class Sniff1(threading.Thread):
	filter = None
	captured = None
	packet = None
	def __init__(self):
		# clear packets buffered by scapy bpf
		sniff(iface=LOCAL_IF, timeout=1)
		super(Sniff1, self).__init__()
	def run(self):
		self.captured = sniff(iface=LOCAL_IF, filter=self.filter,
		    count=1, timeout=5)
		if self.captured:
			self.packet = self.captured[0]

ip=IP(src=FAKE_NET_ADDR, dst=REMOTE_ADDR)

print("Send SYN packet, receive SYN+ACK")
syn=TCP(sport=client, dport=relay, seq=0, flags='S', window=(2**16)-1)
synack=sr1(ip/syn, iface=LOCAL_IF, timeout=5)

if synack is None:
	print("ERROR: No matching SYN+ACK packet received")
	exit(1)

print("Expect spliced SYN")
sniffer = Sniff1();
sniffer.filter = "src %s and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-syn" % (ip.dst, ip.src, server)
sniffer.start()
time.sleep(1)

print("Send ACK packet to finish handshake")
ack=TCP(sport=synack.dport, dport=synack.sport,
    seq=1, ack=synack.seq+1,  flags='A')
send(ip/ack, iface=LOCAL_IF)

sniffer.join(timeout=7)
spliced_syn = sniffer.packet

if spliced_syn is None:
	print("ERROR: No spliced SYN packet received")
	exit(1)

print("Send spliced SYN+ACK packet to finish handshake")
spliced_synack=TCP(sport=spliced_syn.dport, dport=spliced_syn.sport,
    seq=0, ack=spliced_syn.seq+1, flags='SA')
spliced_ack=sr1(ip/spliced_synack, iface=LOCAL_IF)

if spliced_ack is None:
	print("ERROR: No spliced ACK packet received")
	exit(1)
if spliced_ack.seq != spliced_syn.seq+1:
	print("ERROR: Expected seq %d, got %d in spliced ack" %
	    (spliced_syn.seq+1, spliced_ack.seq))
	exit(1)
if spliced_ack.ack != 1:
	print("ERROR: Expected ack %d, got %d in spliced ack" %
	    (1, spliced_ack.ack))
	exit(1)
if spliced_ack.len-20-20 != 0:
	print("ERROR: Expected len %d, got %d in spliced ack" %
	    (0, spliced_ack.len-20-20))
	exit(1)

time.sleep(1)

print("Kill connections with RST")
spliced_rst=TCP(sport=spliced_ack.dport, dport=spliced_ack.sport,
    seq=1, ack=spliced_ack.seq, flags='RA')
send(ip/spliced_rst, iface=LOCAL_IF)
rst=TCP(sport=synack.dport, dport=synack.sport,
    seq=1, ack=synack.seq+1, flags='RA')
send(ip/rst, iface=LOCAL_IF)

exit(0)
