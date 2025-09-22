#!/usr/local/bin/python3
# transfer peer into ESTABLISHED state and check RST after keepalive

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
sniffer = Sniff1(timeout=10)
sniffer.filter = \
    "ip and src %s and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-syn" % \
    (ip.dst, ip.src, tport)
sniffer.start()
time.sleep(1)

print("Connect from remote client.")
os.popen("ssh %s perl %s/client.pl %s %s %u keepalive" % \
    (REMOTE_SSH, CURDIR, ip.dst, ip.src, tport), mode='w')

print("Wait for SYN.")
sniffer.join(timeout=5)
syn=sniffer.packet
if syn is None:
	print("ERROR: No SYN received from remote client.")
	exit(1)

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

print("Start sniffer for keepalive ACK or RST packet from peer.");
sniffer = Sniff1(count=9, timeout=15)
sniffer.filter = \
    "ip and src %s and dst %s and tcp port %u and " \
    "( tcp[tcpflags] = tcp-ack|tcp-rst or tcp[tcpflags] = tcp-ack ) " % \
    (ip.dst, ip.src, tport)
sniffer.start()
time.sleep(1)

print("Wait for keepalive.")
sniffer.join(timeout=15)
keep_ack=sniffer.packet
if keep_ack is None:
	print("ERROR: No keepalive received from remote client.")
	exit(1)

print("Send reset to cleanup the connection.")
new_rst=TCP(sport=ack.dport, dport=ack.sport, flags='RA',
    seq=ack.ack, ack=ack.seq)
send(ip/new_rst)

print("Check keepalive ACK.");
if str(keep_ack[TCP].flags) != 'A':
	print("ERROR: First keepalive is not ACK.")
	exit(1)
if keep_ack.seq != ack.seq-1 or keep_ack.ack != 2:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d " \
	    "in keepalive ACK." % \
	    (ack.seq-1, 2, keep_ack.seq, keep_ack.ack))
	exit(1)

print("Check keepalive RST.");
keep_rst=sniffer.captured[8]
if keep_rst is None:
	print("ERROR: No keepalive RST received from remote client.")
	exit(1)
if str(keep_rst[TCP].flags) != 'RA':
	print("ERROR: Last keepalive is not RST.")
	exit(1)
if keep_rst.seq != ack.seq or keep_rst.ack != 2:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d " \
	    "in keepalive RST." % \
	    (ack.seq, 2, keep_rst.seq, keep_rst.ack))
	exit(1)

exit(0)
