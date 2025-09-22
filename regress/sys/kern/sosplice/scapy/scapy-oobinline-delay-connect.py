#!/usr/local/bin/python3
# send urgent data from client to relay before connecting to server

import os
import re
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

print("Send ACK packet to finish handshake")
ack=TCP(sport=synack.dport, dport=synack.sport,
    seq=1, ack=synack.seq+1,  flags='A')
send(ip/ack, iface=LOCAL_IF)

print("Expect spliced SYN")
sniffer = Sniff1();
sniffer.filter = "src %s and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-syn" % (ip.dst, ip.src, server)
sniffer.start()
time.sleep(1)

print("Send 20 bytes payload and one urgent byte")
data="0123456789Xabcdefghij"
payload=TCP(sport=synack.dport, dport=synack.sport, urgptr=11,
    seq=1, ack=synack.seq+1,  flags='APU')/data
payload_ack=sr1(ip/payload, iface=LOCAL_IF)

if payload_ack is None:
	print("ERROR: No payload ACK packet received")
	exit(1)
if payload_ack.ack != len(data)+1:
	print("ERROR: Expected ack %d, got %d in payload ACK" %
	    (len(data)+1, payload_ack.ack))
	exit(1)

sniffer.join(timeout=7)
spliced_syn = sniffer.packet

if spliced_syn is None:
	print("ERROR: No spliced SYN packet received")
	exit(1)

print("Expect spliced urgent payload")
sniffer = Sniff1();
sniffer.filter = "src %s and dst %s and tcp port %u " \
    "and tcp[tcpflags] = tcp-ack|tcp-urg" % (ip.dst, ip.src, server)
sniffer.start()
time.sleep(1)

print("Wait for splicing syscall, grep it in relay log")
def loggrep(file, regex, timeout):
	for i in range(timeout):
		for line in open(file, 'r'):
			if re.search(regex, line):
				return line
		time.sleep(1)
	return None
if not loggrep("relay.log", "Spliced", 5):
	print("ERROR: Relay did not splice")
	exit(1)

print("Send spliced SYN+ACK packet to finish handshake")
spliced_synack=TCP(sport=spliced_syn.dport, dport=spliced_syn.sport,
    seq=0, ack=spliced_syn.seq+1, flags='SA')
spliced_ack=sr1(ip/spliced_synack, iface=LOCAL_IF)

if spliced_ack is None:
	print("ERROR: No spliced ACK packet received")
	exit(1)

sniffer.join(timeout=7)
spliced_payload = sniffer.packet

if spliced_payload is None:
	print("ERROR: No spliced urgent payload packet received")
	exit(1)
if spliced_payload.seq != spliced_ack.seq:
	print("ERROR: Expected seq %d, got %d in spliced payload" %
	    (spliced_ack.seq, spliced_payload.seq))
	exit(1)
if spliced_payload.urgptr != 11:
	print("ERROR: Expected urgptr %d, got %d in spliced payload" %
	    (11, spliced_payload.urgptr))
	exit(1)

print("Kill connections with RST")
spliced_rst=TCP(sport=spliced_ack.dport, dport=spliced_ack.sport,
    seq=1, ack=spliced_ack.seq, flags='RA')
send(ip/spliced_rst, iface=LOCAL_IF)
rst=TCP(sport=synack.dport, dport=synack.sport,
    seq=payload_ack.ack, ack=synack.seq+1, flags='RA')
send(ip/rst, iface=LOCAL_IF)

exit(0)
