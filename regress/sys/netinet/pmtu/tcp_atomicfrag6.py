#!/usr/local/bin/python3

import os
import threading
from addr import *
from scapy.all import *

class Sniff(threading.Thread):
	filter = None
	captured = None
	packet = None
	def __init__(self):
		# clear packets buffered by scapy bpf
		sniff(iface=LOCAL_IF, timeout=1)
		super(Sniff, self).__init__()
	def run(self):
		self.captured = sniff(iface=LOCAL_IF, filter=self.filter,
		    timeout=3)
		if self.captured:
			self.packet = self.captured[0]

e=Ether(src=LOCAL_MAC, dst=REMOTE_MAC)
ip6=IPv6(src=FAKE_NET_ADDR6, dst=REMOTE_ADDR6)
tport=os.getpid() & 0xffff

print("Send SYN packet, receive SYN+ACK.")
syn=TCP(sport=tport, dport='chargen', seq=1, flags='S', window=(2**16)-1)
synack=srp1(e/ip6/syn, iface=LOCAL_IF, timeout=5)

if synack is None:
	print("ERROR: No SYN+ACK from chargen server received.")
	exit(1)

print("Send ACK packet, receive chargen data.")
ack=TCP(sport=synack.dport, dport=synack.sport, seq=2, flags='A',
    ack=synack.seq+1, window=(2**16)-1)
data=srp1(e/ip6/ack, iface=LOCAL_IF, timeout=5)

if data is None:
	print("ERROR: No data from chargen server received.")
	exit(1)

print("Fill our receive buffer.")
time.sleep(1)

# srp1 cannot be used, fragment answer will not match outgoing ICMP6 packet
sniffer = Sniff()
sniffer.filter = \
    "ip6 and src "+ip6.dst+" and dst "+ip6.src+" and proto ipv6-frag"
sniffer.start()
time.sleep(1)

print("Send ICMP6 packet too big packet with MTU 1272.")
icmp6=ICMPv6PacketTooBig(mtu=1272)/data.payload
sendp(e/IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/icmp6, iface=LOCAL_IF)

print("Path MTU discovery will not resend data, ICMP6 packet is ignored.")
sniffer.join(timeout=5)

print("IPv6 atomic fragments must not be generated.")
frag=None
for a in sniffer.captured:
	fh=a.payload.payload
	if fh.offset != 0 or fh.nh != (ip6/syn).nh:
		continue
	th=fh.payload
	if th.sport != syn.dport or th.dport != syn.sport:
		continue
	frag=a
	break

if frag is not None:
	print("ERROR: Matching IPv6 fragment TCP answer found.")
	exit(1)

print("Send ACK again to trigger retransmit.")
data=srp1(e/ip6/ack, iface=LOCAL_IF, timeout=5)

if data is None:
	print("ERROR: No data retransmit from chargen server received.")
	exit(1)

print("Cleanup the other's socket with a reset packet.")
rst=TCP(sport=synack.dport, dport=synack.sport, seq=2, flags='AR',
    ack=synack.seq+1)
sendp(e/ip6/rst, iface=LOCAL_IF)

len = data.plen + len(IPv6())
print("len=%d" % len)
if len != 1500:
	print("ERROR: TCP data packet len is %d, expected 1500." % len)
	exit(1)

exit(0)
