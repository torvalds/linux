#!/usr/local/bin/python3
# transfer peer into SYN_SENT state and check retransmit of SYN

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

print("Start sniffer for SYN packet from peer.");
sniffer = Sniff1(count=2, timeout=10)
sniffer.filter = \
    "ip and src %s and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-syn" % \
    (ip.dst, ip.src, tport)
sniffer.start()
time.sleep(1)

print("Connect from remote client.")
os.popen("ssh %s perl %s/client.pl %s %s %u" % \
    (REMOTE_SSH, CURDIR, ip.dst, ip.src, tport), mode='w')

print("Wait for SYN and its retransmit.")
sniffer.join(timeout=10)
syn=sniffer.packet
if syn is None:
	print("ERROR: No SYN received from remote client.")
	exit(1)

print("Check peer is in SYN_SENT state.")
with os.popen("ssh "+REMOTE_SSH+" netstat -vnp tcp") as netstat:
	with open("netstat-synsent.log", 'w') as log:
		for line in netstat:
			if "%s.%d" % (FAKE_NET_ADDR, tport) in line:
				print(line)
				log.write(line)

synack=TCP(sport=syn.dport, dport=syn.sport, flags='SA',
    seq=1, ack=syn.seq+1, window=(2**16)-1)
ack=sr1(ip/synack, timeout=5)
if ack is None:
	print("ERROR: No ACK from remote client received.")
	exit(1)
if ack.seq != syn.seq+1 or ack.ack != 2:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d in ACK." % \
	    (syn.seq+1, 2, ack.seq, ack.ack))
	exit(1)

print("Send reset to cleanup the connection.")
new_rst=TCP(sport=ack.dport, dport=ack.sport, flags='RA',
    seq=ack.ack, ack=ack.seq)
send(ip/new_rst)

print("Check retransmit of SYN.");
rxmit_syn = sniffer.captured[1]
if rxmit_syn is None:
	print("ERROR: No SYN retransmitted from netstat client.")
	exit(1)
if rxmit_syn.seq != syn.seq or rxmit_syn.ack != syn.ack:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d " \
	    "in rxmit SYN." % \
	    (syn.seq, syn.ack, rxmit_syn.seq, rxmit_syn.ack))
	exit(1)

exit(0)
