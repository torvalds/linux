#!/usr/local/bin/python3
# transfer peer from SYN_SENT to SYN_RCVD state and check retransmit of SYN+ACK
# from LISTEN state SYN_RCVD is cannot be reached as SYN cache handles it

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
os.popen("ssh %s perl %s/client.pl %s %s %u" % \
    (REMOTE_SSH, CURDIR, ip.dst, ip.src, tport), mode='w')

print("Wait for SYN.")
sniffer.join(timeout=10)
syn=sniffer.packet
if syn is None:
	print("ERROR: No SYN received from remote client.")
	exit(1)

print("Start sniffer for SYN+ACK packet from peer.");
sniffer = Sniff1(count=2, timeout=10)
sniffer.filter = \
    "ip and src %s and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-syn|tcp-ack" % \
    (ip.dst, ip.src, tport)
sniffer.start()
time.sleep(1)

print("Send SYN packet, receive SYN+ACK.")
synsyn=TCP(sport=syn.dport, dport=syn.sport, flags='S',
    seq=1, ack=syn.seq+1, window=(2**16)-1)
send(ip/synsyn)

print("Wait for SYN+ACK and its retransmit.")
sniffer.join(timeout=10)
synack=sniffer.packet
if synack is None:
	print("ERROR: No SYN+ACK from remote client received.")
	exit(1)
if synack.seq != syn.seq or synack.ack != 2:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d " \
	    "in SYN+ACK." % \
	    (syn.seq, 2, synack.seq, synack.ack))
	exit(1)

print("Check peer is in SYN_RCVD state.")
with os.popen("ssh "+REMOTE_SSH+" netstat -vnp tcp") as netstat:
	with open("netstat-synrcvd.log", 'w') as log:
		for line in netstat:
			if "%s.%d" % (FAKE_NET_ADDR, tport) in line:
				print(line)
				log.write(line)

print("Send ACK packet to finish handshake.")
ack=TCP(sport=synack.dport, dport=synack.sport, flags='A',
    seq=2, ack=synack.seq+1, window=(2**16)-1)
send(ip/ack)

print("Check peer is in ESTABLISHED state.")
with os.popen("ssh "+REMOTE_SSH+" netstat -vnp tcp") as netstat:
	with open("netstat-synrcvd.log", 'a') as log:
		for line in netstat:
			if "%s.%d" % (FAKE_NET_ADDR, tport) in line:
				print(line)
				log.write(line)

print("Send reset to cleanup the connection.")
new_rst=TCP(sport=synack.dport, dport=synack.sport, flags='RA',
    seq=ack.seq, ack=ack.ack)
send(ip/new_rst)

print("Check retransmit of SYN+ACK.");
rxmit_synack = sniffer.captured[1]
if rxmit_synack is None:
	print("ERROR: No SYN+ACK retransmitted from remote client.")
	exit(1)
if rxmit_synack.ack != 2:
	print("ERROR: expecting ack %d, got ack %d in rxmit SYN+ACK." % \
	    (2, rxmit_synack.ack))
	exit(1)
if rxmit_synack.seq != syn.seq or rxmit_synack.ack != 2:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d " \
	    "in SYN+ACK." % \
	    (syn.seq, 2, rxmit_synack.seq, rxmit_synack.ack))
	exit(1)

exit(0);
