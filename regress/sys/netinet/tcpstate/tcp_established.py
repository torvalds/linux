#!/usr/local/bin/python3
# transfer peer into ESTABLISHED state and check retransmit of data

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
syn=TCP(sport=tport, dport='echo', flags='S', seq=1, window=(2**16)-1)
synack=sr1(ip/syn, timeout=5)
if synack is None:
	print("ERROR: No SYN+ACK from echo server received.")
	exit(1)

print("Send ACK packet to finish handshake.")
ack=TCP(sport=synack.dport, dport=synack.sport, flags='A',
    seq=2, ack=synack.seq+1, window=(2**16)-1)
send(ip/ack)

print("Start sniffer to get data retransmit from peer.");
sniffer = Sniff1(count=3, timeout=10)
sniffer.filter = \
    "ip and src %s and tcp port %u and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-ack|tcp-push" % \
    (ip.dst, syn.dport, ip.src, syn.sport)
sniffer.start()
time.sleep(1)

payload=b"abcdefgh01234567"
paylen=len(payload)
print("Send data to trigger echo.")
data=TCP(sport=syn.sport, dport=syn.dport, flags='AP',
    seq=2, ack=synack.seq+1, window=(2**16)-1)/payload
data_ack=sr1(ip/data, timeout=5)
if data_ack is None:
	print("ERROR: No data ACK received from echo server.")
	exit(1)
if data_ack.seq != synack.seq+1 or data_ack.ack != 2+paylen:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d " \
	    "in data ACK." % \
	    (synack.seq+1, 2+paylen, data_ack.seq, data_ack.ack))
	exit(1)

print("Send ACK for echo packet, but with one squence less.");
echo_ack=TCP(sport=synack.dport, dport=synack.sport, flags='A',
    seq=2+paylen, ack=data_ack.seq+paylen-1, window=(2**16)-1)
echo=sr1(ip/echo_ack, timeout=5);
if echo is None:
	print("ERROR: No truncated echo received from echo server.")
	exit(1)
if echo.getlayer(TCP).flags != 'AP':
	print("ERROR: expecting PSH, got flag '%s' in echo." % \
	    (echo.getlayer(TCP).flags))
	exit(1)
tcplen = echo.len - echo.ihl*4 - echo.dataofs*4
if echo.seq != synack.seq+1+paylen-1 or echo.ack != 2+paylen or \
    tcplen != 1:
	print("ERROR: expecting seq %d ack %d len %d, " \
	    "got seq %d ack %d len %d in echo." % \
	    (synack.seq+1+paylen-1, 2+paylen, 1, echo.seq, echo.ack, tcplen))
	exit(1)

print("Wait for echo and its retransmit.")
sniffer.join(timeout=10)

print("Check peer is in ESTABLISHED state.")
with os.popen("ssh "+REMOTE_SSH+" netstat -vnp tcp") as netstat:
	with open("netstat-established.log", 'w') as log:
		for line in netstat:
			if "%s.%d" % (FAKE_NET_ADDR, tport) in line:
				print(line)
				log.write(line)

print("Send reset to cleanup the connection.")
new_rst=TCP(sport=synack.dport, dport=synack.sport, flags='RA',
    seq=data_ack.ack, ack=data_ack.seq)
send(ip/new_rst)

print("Check retransmit of echo.");
rxmit_echo = sniffer.captured[2]
if rxmit_echo is None:
	print("ERROR: No echo retransmitted from echo server.")
	exit(1)
if rxmit_echo.seq != synack.seq+1+paylen-1 or rxmit_echo.ack != 2+paylen:
	print("ERROR: expecting seq %d ack %d, got seq %d ack %d " \
	    "in rxmit echo." % \
	    (synack.seq+1+paylen-1, 2+paylen, rxmit_echo.seq, rxmit_echo.ack))
	exit(1)

exit(0)
