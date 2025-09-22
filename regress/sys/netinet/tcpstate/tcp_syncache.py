#!/usr/local/bin/python3
# transfer peer into SYN cache and check retransmit of SYN+ACK

import os
import threading
from addr import *
from scapy.all import *

class Sniff1(threading.Thread):
	filter = None
	captured = None
	packet = None
	count = None
	timeout = None
	def __init__(self, count=1, timeout=3):
		self.count = count
		self.timeout = timeout
		# clear packets buffered by scapy bpf
		sniff(iface=LOCAL_IF, timeout=1)
		super(Sniff1, self).__init__()
	def run(self):
		self.captured = sniff(iface=LOCAL_IF, filter=self.filter,
		    count=self.count, timeout=self.timeout)
		if self.captured:
			self.packet = self.captured[0]

ip=IP(src=FAKE_NET_ADDR, dst=REMOTE_ADDR)
tport=os.getpid() & 0xffff

print("Start sniffer for SYN+ACK packet from peer.");
sniffer = Sniff1(count=2, timeout=10)
sniffer.filter = \
    "ip and src %s and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-syn|tcp-ack" % \
    (ip.dst, ip.src, tport)
sniffer.start()
time.sleep(1)

print("Send SYN packet, receive SYN+ACK.")
syn=TCP(sport=tport, dport='discard', flags='S', seq=1, window=(2**16)-1)
send(ip/syn)

print("Wait for SYN+ACK and its retransmit.")
sniffer.join(timeout=10)
synack=sniffer.packet
if synack is None:
	print("ERROR: No SYN+ACK from discard server received.")
	exit(1)
if synack.ack != 2:
	print("ERROR: expecting ack %d, got ack %d in SYN+ACK." % \
	    (2, synack.ack))
	exit(1)

print("Check peer is in SYN cache.")
with os.popen("ssh "+REMOTE_SSH+" netstat -snp tcp") as netstat:
	with open("netstat-syncache.log", 'w') as log:
		for line in netstat:
			if "SYN,ACK" in line:
				print(line)
				log.write(line)

print("Send ACK packet to finish handshake.")
ack=TCP(sport=synack.dport, dport=synack.sport, flags='A',
    seq=2, ack=synack.seq+1, window=(2**16)-1)
send(ip/ack)

print("Send reset to cleanup the connection.")
new_rst=TCP(sport=synack.dport, dport=synack.sport, flags='RA',
    seq=ack.seq, ack=ack.ack)
send(ip/new_rst)

print("Check retransmit of SYN+ACK.");
rxmit_synack = sniffer.captured[1]
if rxmit_synack is None:
	print("ERROR: No SYN+ACK retransmitted from discard server.")
	exit(1)
if rxmit_synack.ack != 2:
	print("ERROR: expecting ack %d, got ack %d in rxmit SYN+ACK." % \
	    (2, rxmit_synack.ack))
	exit(1)

exit(0);
