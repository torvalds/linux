#!/usr/local/bin/python3
# $OpenBSD: process_ra.py,v 1.3 2020/12/25 14:25:58 bluhm Exp $

from IfInfo import IfInfo
from Slaacctl import ShowInterface
import subprocess
import sys
from scapy.all import *
import unittest

rtadv_if = IfInfo(sys.argv[1])
slaac_if = IfInfo(sys.argv[2])
sock = sys.argv[3]

eth = Ether(src=rtadv_if.mac)
ip = IPv6(dst="ff02::1", src=rtadv_if.ll)
ra = ICMPv6ND_RA(prf='Medium (default)', routerlifetime=1800)
pref = ICMPv6NDOptPrefixInfo(prefixlen=64, prefix='2001:db8:1::',
    validlifetime=2592000, preferredlifetime=604800, L=1, A=1)
mtu = ICMPv6NDOptMTU(mtu=1500)
rdnss = ICMPv6NDOptRDNSS(lifetime=86400, dns=['2001:db8:53::a',
    '2001:db8:53::b'])
dnssl = ICMPv6NDOptDNSSL(lifetime=86400, searchlist=['invalid', 'home.invalid'])

p = eth/ip/ra/pref/mtu/rdnss/dnssl

sendp(p, iface=rtadv_if.ifname, verbose=0)

slaac_show_interface = ShowInterface(slaac_if.ifname, sock, debug=0)

class TestRouterAdvertisementParsing(unittest.TestCase):
	def test_number_ras(self):
		self.assertEqual(len(slaac_show_interface.RAs), 1)

	def test_number_addr_proposals(self):
		self.assertEqual(len(slaac_show_interface.addr_proposals), 2)

	def test_number_def_router_proposals(self):
		self.assertEqual(len(
		    slaac_show_interface.def_router_proposals), 1)

	def test_number_rdns_proposals(self):
		self.assertEqual(len(
		    slaac_show_interface.rdns_proposals), 1)

	def test_number_rdns_proposals_rnds(self):
		self.assertEqual(len(
		    slaac_show_interface.rdns_proposals[0]['rdns']), 2)

if __name__ == '__main__':
	suite = unittest.TestLoader().loadTestsFromTestCase(
	    TestRouterAdvertisementParsing)
	if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
		print(slaac_show_interface)
		sys.exit(1)
