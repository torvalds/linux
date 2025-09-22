#!/usr/local/bin/python3

import os
import threading
import string
import random
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
uport=os.getpid() & 0xffff
# inetd ignores UDP packets from privileged port or nfs
if uport < 1024 or uport == 2049:
	uport+=1024

print("Send UDP packet with 1200 octets payload, receive echo.")
data=''.join(random.choice(string.ascii_uppercase + string.ascii_lowercase +
    string.digits) for _ in range(1200))
udp=UDP(sport=uport, dport='echo')/data
echo=srp1(e/ip6/udp, iface=LOCAL_IF, timeout=5)

if echo is None:
	print("ERROR: No UDP answer from echo server received.")
	exit(1)

print("Send ICMP6 packet too big packet with MTU 1272.")
icmp6=ICMPv6PacketTooBig(mtu=1272)/echo.payload
sendp(e/IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/icmp6, iface=LOCAL_IF)

print("Clear route cache at echo socket by sending from different address.")
sendp(e/IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/udp, iface=LOCAL_IF)

# srp1 cannot be used, fragment answer will not match on outgoing UDP packet
sniffer = Sniff()
sniffer.filter = \
    "ip6 and src "+ip6.dst+" and dst "+ip6.src+" and proto ipv6-frag"
sniffer.start()
time.sleep(1)

print("Send UDP packet with 1200 octets payload.")
sendp(e/ip6/udp, iface=LOCAL_IF)

print("Path MTU discovery will not send UDP atomic fragment.")
sniffer.join(timeout=5)

print("IPv6 atomic fragments must not be generated.")
frag=None
for a in sniffer.captured:
	fh=a.payload.payload
	if fh.offset != 0 or fh.nh != (ip6/udp).nh:
		continue
	uh=fh.payload
	if uh.sport != udp.dport or uh.dport != udp.sport:
		continue
	frag=a
	break

if frag is not None:
	print("ERROR: Matching IPv6 fragment UDP answer found.")
	exit(1)

print("Send echo again and expect reply without fragmentation.")
reply=srp1(e/IPv6(src=LOCAL_ADDR6, dst=REMOTE_ADDR6)/udp, iface=LOCAL_IF)

print("UDP echo has IPv6 and UDP header, so expected payload len is 1248.")
elen = reply.plen + len(IPv6())
print("rlen=%d" % elen)
if elen != 1248:
	print("ERROR: UDP reply payload len is %d, expected 1248." % elen)
	exit(1)

exit(0)
