#!/usr/local/bin/python3
# transfer peer into LAST_ACK state and check retransmit of FIN

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

print("Send SYN packet, receive SYN+ACK.")
syn=TCP(sport=tport, dport='discard', flags='S', seq=1, window=(2**16)-1)
synack=sr1(ip/syn, timeout=5)
if synack is None:
	print("ERROR: No SYN+ACK from discard server received.")
	exit(1)

print("Send ACK packet to finish handshake.")
ack=TCP(sport=synack.dport, dport=synack.sport, flags='A',
    seq=2, ack=synack.seq+1, window=(2**16)-1)
send(ip/ack)

print("Start sniffer to get FIN packet from peer.");
sniffer = Sniff1(count=2, timeout=10)
sniffer.filter = \
    "ip and src %s and tcp port %u and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-fin|tcp-ack" % \
    (ip.dst, syn.dport, ip.src, syn.sport)
sniffer.start()
time.sleep(1)

print("Send FIN packet to close connection.");
send_fin=TCP(sport=synack.dport, dport=synack.sport, flags='FA',
    seq=2, ack=synack.seq+1, window=(2**16)-1)
recv_ack=sr1(ip/send_fin, timeout=5)
if recv_ack is None:
	print("ERROR: No ACK for FIN from discard server received.")
	exit(1)
if recv_ack.getlayer(TCP).flags != 'A':
	print("ERROR: expecting ACK, got flag '%s' in recv FIN." % \
	    (recv_ack.getlayer(TCP).flags))
	exit(1)
if recv_ack.seq != synack.seq+1 or recv_ack.ack != 3:
	print("ERROR: expecting seq %d ack %d, " \
	    "got seq %d ack %d in recv ACK." % \
	    (synack.seq+1, 3, recv_ack.seq, recv_ack.ack))
	exit(1)

print("Wait for FIN and its retransmit.")
sniffer.join(timeout=10)
recv_fin = sniffer.packet
if recv_fin is None:
	print("ERROR: No FIN received from discard server.")
	exit(1)
if recv_fin.seq != synack.seq+1 or recv_fin.ack != 3:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d " \
	    "in recv FIN." % \
	    (synack.seq+1, 3, recv_fin.seq, recv_fin.ack))
	exit(1)

print("Check peer is in LASTACK state.")
with os.popen("ssh "+REMOTE_SSH+" netstat -vnp tcp") as netstat:
	with open("netstat-lastack.log", 'w') as log:
		for line in netstat:
			if "%s.%d" % (FAKE_NET_ADDR, tport) in line:
				print(line)
				log.write(line)

print("Send ACK for FIN packet to close connection.");
send_ack=TCP(sport=synack.dport, dport=synack.sport, flags='A',
    seq=3, ack=recv_fin.seq+1, window=(2**16)-1)
send(ip/send_ack)

print("Check retransmit of FIN.");
rxmit_fin = sniffer.captured[1]
if rxmit_fin is None:
	print("ERROR: No FIN retransmitted from discard server.")
	exit(1)
if rxmit_fin.seq != synack.seq+1 or rxmit_fin.ack != 3:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d " \
	    "in rxmit FIN." % \
	    (synack.seq+1, 3, rxmit_fin.seq, rxmit_fin.ack))
	exit(1)

exit(0);
