#!/usr/local/bin/python3

import os
import threading
from addr import *
from scapy.all import *

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
		    count=1, timeout=3)
		if self.captured:
			self.packet = self.captured[0]

ip=IP(src=FAKE_NET_ADDR, dst=REMOTE_ADDR)
tport=os.getpid() & 0xffff

print("Send SYN packet, receive SYN+ACK.")
syn=TCP(sport=tport, dport='chargen', seq=1, flags='S', window=(2**16)-1)
synack=sr1(ip/syn, iface=LOCAL_IF, timeout=5)

if synack is None:
	print("ERROR: No SYN+ACK from chargen server received.")
	exit(1)

print("Send ACK packet, receive chargen data.")
ack=TCP(sport=synack.dport, dport=synack.sport, seq=2, flags='A',
    ack=synack.seq+1, window=(2**16)-1)
data=sr1(ip/ack, iface=LOCAL_IF, timeout=5)

if data is None:
	print("ERROR: No data from chargen server received.")
	exit(1)

print("Fill our receive buffer.")
time.sleep(1)

# sr1 cannot be used, TCP data will not match outgoing ICMP packet
sniffer = Sniff1()
sniffer.filter = \
    "ip and src %s and tcp port %u and dst %s and tcp port %u" % \
    (ip.dst, syn.dport, ip.src, syn.sport)
sniffer.start()
time.sleep(1)

print("Send ICMP fragmentation needed packet with MTU 1300.")
icmp=ICMP(type="dest-unreach", code="fragmentation-needed",
    nexthopmtu=1300)/data
send(IP(src=LOCAL_ADDR, dst=REMOTE_ADDR)/icmp, iface=LOCAL_IF)

print("Path MTU discovery will resend first data with length 1300.")
sniffer.join(timeout=5)
ans = sniffer.packet

if len(ans) == 0:
	print("ERROR: No data retransmit from chargen server received.")
	exit(1)
data=ans[0]

print("Cleanup the other's socket with a reset packet.")
rst=TCP(sport=synack.dport, dport=synack.sport, seq=2, flags='AR',
    ack=synack.seq+1)
send(ip/rst, iface=LOCAL_IF)

len = data.len
print("len=%d" % len)
if len != 1300:
	print("ERROR: TCP data packet len is %d, expected 1300." % len)
	exit(1)

exit(0)
