#!/usr/local/bin/python3
# $OpenBSD: sniff_sol.py,v 1.2 2020/12/25 14:25:58 bluhm Exp $

# Copyright (c) 2017 Florian Obser <florian@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

import subprocess
import threading
from scapy.all import *

class Sniffer(threading.Thread):
	executor = None
	p = None
	# pcap cannot access icmp6, type 133 = router solicitation
	filter = "icmp6 and ip6[40]=133"
	def run(self):
		answer = sniff(iface='pair1', count=1, timeout=30,
		    filter=self.filter)
		executor.stopit()
		if answer:
			self.p = answer[0]

class Executor(threading.Thread):
	event = threading.Event()
	def stopit(self):
		self.event.set()

	def run(self):
		for sols in range(0, 30):
			subprocess.call(['slaacctl', '-s', sys.argv[1],
			    'send', 'sol', 'pair2'])
			if self.event.wait(1):
				break

sniffer = Sniffer()
executor = Executor()

sniffer.executor = executor

sniffer.start()
executor.start()

sniffer.join(timeout=30)
executor.join(timeout=30)

p = sniffer.p

if p is None:
	print("no packet sniffed")
	exit(2)

if p.type != ETH_P_IPV6:
	print("unexpected ethertype: {0}".format(p.type))
	exit(1)

if not p.payload.nh in ipv6nh or ipv6nh[p.payload.nh] != 'ICMPv6':
	print("unexpected next header: {0}".format(p.payload.nh))
	exit(1)

if p[IPv6].hlim != 255:
	print("invalid hlim: {0}".format(p[IPv6].hlim))
	exit(1)

if p[IPv6].dst != 'ff02::2':
	print("invalid IPv6 destination: {0}".format(p[IPv6].dst))
	exit(1)

if 'ICMPv6ND_RS' not in p[IPv6]:
	print("no router solicitation found")
	exit(1)


if 'ICMPv6NDOptSrcLLAddr' not in p[IPv6][ICMPv6ND_RS]:
	print("no Source Link-Layer Address option")
	exit(1)

if p[Ether].src != p[IPv6][ICMPv6ND_RS].lladdr:
	print("src mac ({0}) != lladdr option ({0})".format(p[Ether].src,
	    p[IPv6][ICMPv6ND_RS].lladdr))
	exit(1)

print("received router solicitation")
exit(0)
