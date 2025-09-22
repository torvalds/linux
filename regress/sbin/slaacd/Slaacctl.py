# $OpenBSD: Slaacctl.py,v 1.5 2024/12/25 14:57:47 sthen Exp $

# Copyright (c) 2017 Florian Obser <florian@openbsd.org>
# Copyright (c) 2020 Alexander Bluhm <bluhm@openbsd.org>
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

import pprint
import subprocess
import re

class ShowInterface(object):
	def __init__(self, ifname, sock, debug=0):
		self.ifname = ifname
		self.sock = sock
		self.debug = debug
		self.index = None
		self.running = None
		self.temporary = None
		self.lladdr = None
		self.linklocal = None
		self.RAs = []
		self.addr_proposals = []
		self.def_router_proposals = []
		self.rdns_proposals = []
		self.out = subprocess.check_output(['slaacctl', '-s', self.sock,
		    'sh', 'in', self.ifname], encoding='UTF-8')
		self.parse(self.out)

	def __str__(self):
		rep = dict()
		iface = dict()
		rep[self.ifname] = iface
		iface['index'] = self.index
		iface['running'] = self.running
		iface['temporary'] = self.temporary
		iface['lladdr'] = self.lladdr
		iface['linklocal'] = self.linklocal
		iface['RAs'] = self.RAs
		iface['addr_proposals'] = self.addr_proposals
		iface['def_router_proposals'] = self.def_router_proposals
		iface['rdns_proposals'] = self.rdns_proposals
		return (pprint.pformat(rep, indent=4))

	def parse(self, str):
		state = 'START'
		ra = None
		prefix = None
		addr_proposal = None
		def_router_proposal = None
		rdns_proposal = None
		lines = str.splitlines()
		for line in lines:
			if self.debug == 1:
				print(line)
			if re.match(r"^\s*$", line):
				pass
			elif state == 'START':
				ifname = re.match(r"^(\w+):", line).group(1)
				if ifname != self.ifname:
					raise ValueError("unexpected interface "
					    + "name: " + ifname)
				state = 'IFINFO'
			elif state == 'IFINFO':
				m = re.match(r"^\s+index:\s+(\d+)\s+running:"
				    + r"\s+(\w+)\s+temporary:\s+(\w+)", line)
				self.index = m.group(1)
				self.running = m.group(2)
				self.temporary = m.group(3)
				state = 'IFLLADDR'
			elif state == 'IFLLADDR':
				self.lladdr = re.match(r"^\s+lladdr:\s+(.*)",
				    line).group(1)
				state = 'IFLINKLOCAL'
			elif state == 'IFLINKLOCAL':
				self.linklocal = re.match(r"^\s+inet6:\s+(.*)",
				    line).group(1)
				state = 'IFDONE'
			elif state == 'IFDONE':
				is_ra = re.match(r"^\s+Router Advertisement "
				    + r"from\s+(.*)", line)
				is_addr_proposal = re.match(r"^\s+Address "
				    + "proposals", line)
				if is_ra:
					ra = dict()
					ra['prefixes'] = []
					ra['rdns'] = []
					ra['search'] = []
					ra['from'] = is_ra.group(1)
					self.RAs.append(ra)
					state = 'RASTART'
				elif is_addr_proposal:
					state = 'ADDRESS_PROPOSAL'
			elif state == 'RASTART':
				m = re.match(r"\s+received:\s+(.*);\s+(\d+)s "
				    + "ago", line)
				ra['received'] = m.group(1)
				ra['ago'] = m.group(2)
				state = 'RARECEIVED'
			elif state == 'RARECEIVED':
				m = re.match(r"\s+Cur Hop Limit:\s+(\d+), M: "
				    + r"(\d+), O: (\d+), "
				    + r"Router Lifetime:\s+(\d+)s", line)
				ra['cur_hop_limit'] = m.group(1)
				ra['M'] = m.group(2)
				ra['O'] = m.group(3)
				ra['lifetime'] = m.group(4)
				state = 'RACURHOPLIMIT'
			elif state == 'RACURHOPLIMIT':
				ra['preference'] = re.match(r"^\s+Default "
				    + r"Router Preference:\s+(.*)",
				    line).group(1)
				state = 'RAPREFERENCE'
			elif state == 'RAPREFERENCE':
				m = re.match(r"^\s+Reachable Time:\s+(\d+)ms, "
				    + r"Retrans Timer:\s+(\d+)ms", line)
				ra['reachable_time'] = m.group(1)
				ra['retrans_timer'] = m.group(2)
				state = 'RAOPTIONS'
			elif state == 'RAOPTIONS':
				is_addr_proposal = re.match(r"^\s+Address "
				    + "proposals", line)
				is_rdns = re.match(r"^\s+rdns: (.*), "
				    + r"lifetime:\s+(\d+)", line)
				is_search = re.match(r"^\s+search: (.*), "
				    + r"lifetime:\s+(\d+)", line)
				is_prefix = re.match(r"^\s+prefix:\s+(.*)", line)
				if is_addr_proposal:
					state = 'ADDRESS_PROPOSAL'
				elif is_prefix:
					prefix = dict()
					ra['prefixes'].append(prefix)
					prefix['prefix'] = is_prefix.group(1)
					state = 'PREFIX'
				elif is_rdns:
					rdns = dict()
					ra['rdns'].append(rdns)
					rdns['addr'] = is_rdns.group(1)
					rdns['lifetime'] = is_rdns.group(2)
					state = 'RAOPTIONS'
				elif is_search:
					search = dict()
					ra['search'].append(search)
					search['search'] = is_search.group(1)
					search['lifetime'] = is_search.group(2)
					state = 'RAOPTIONS'
			elif state == 'PREFIX':
				m = re.match(r"^\s+On-link: (\d+), "
				    + "Autonomous address-configuration: "
				    + r"(\d+)", line)
				prefix['on_link'] = m.group(1)
				prefix['autonomous'] = m.group(2)
				state = 'PREFIX_ONLINK'
			elif state == 'PREFIX_ONLINK':
				m = re.match(r"^\s+vltime:\s+(\d+|infinity), "
				    + r"pltime:\s+(\d+|infinity)", line)
				prefix['vltime'] = m.group(1)
				prefix['pltime'] = m.group(2)
				state = 'RAOPTIONS'
			elif state == 'ADDRESS_PROPOSAL':
				is_id = re.match(r"^\s+id:\s+(\d+), "
				    + r"state:\s+(.+), temporary: (.+)", line)
				is_defrouter = re.match(r"\s+Default router "
				    + "proposals", line)
				if is_id:
					addr_proposal = dict()
					self.addr_proposals.append(
					    addr_proposal)
					addr_proposal['id'] = is_id.group(1)
					addr_proposal['state'] = is_id.group(2)
					addr_proposal['temporary'] = \
					    is_id.group(3)
					state = 'ADDRESS_PROPOSAL_LIFETIME'
				elif is_defrouter:
					state = 'DEFAULT_ROUTER'
			elif state == 'ADDRESS_PROPOSAL_LIFETIME':
				m = re.match(r"^\s+vltime:\s+(\d+), "
				    + r"pltime:\s+(\d+), "
				    + r"timeout:\s+(\d+)s", line)
				addr_proposal['vltime'] = m.group(1)
				addr_proposal['pltime'] = m.group(2)
				addr_proposal['timeout'] = m.group(3)
				state = 'ADDRESS_PROPOSAL_UPDATED'
			elif state == 'ADDRESS_PROPOSAL_UPDATED':
				m = re.match(r"^\s+updated:\s+(.+);\s+(\d+)s "
				    + "ago", line)
				addr_proposal['updated'] = m.group(1)
				addr_proposal['updated_ago'] = m.group(2)
				state = 'ADDRESS_PROPOSAL_ADDR_PREFIX'
			elif state == 'ADDRESS_PROPOSAL_ADDR_PREFIX':
				m = re.match(r"^\s+(.+), (.+)", line)
				addr_proposal['addr'] = m.group(1)
				addr_proposal['prefix'] = m.group(2)
				state = 'ADDRESS_PROPOSAL'
			elif state == 'DEFAULT_ROUTER':
				is_id = re.match(r"^\s+id:\s+(\d+), "
				    + r"state:\s+(.+)", line)
				is_rdns = re.match(r"\s+rDNS proposals", line)
				if is_id:
					def_router_proposal = dict()
					self.def_router_proposals.append(
					    def_router_proposal)
					def_router_proposal['id'] = \
					    is_id.group(1)
					def_router_proposal['state'] = \
					    is_id.group(2)
					state = 'DEFAULT_ROUTER_PROPOSAL'
				elif is_rdns:
					state = 'RDNS'
				else:
					state = 'DONE'
			elif state == 'DEFAULT_ROUTER_PROPOSAL':
				m = re.match(r"^\s+router: (.+)", line)
				def_router_proposal['router'] = m.group(1)
				state = 'DEFAULT_ROUTER_PROPOSAL_ROUTER'
			elif state == 'DEFAULT_ROUTER_PROPOSAL_ROUTER':
				m = re.match(r"^\s+router lifetime:\s+(\d)",
				    line)
				def_router_proposal['lifetime'] = m.group(1)
				state = 'DEFAULT_ROUTER_PROPOSAL_LIFETIME'
			elif state == 'DEFAULT_ROUTER_PROPOSAL_LIFETIME':
				m = re.match(r"^\s+Preference: (.+)", line)
				def_router_proposal['pref'] = m.group(1)
				state = 'DEFAULT_ROUTER_PROPOSAL_PREF'
			elif state == 'DEFAULT_ROUTER_PROPOSAL_PREF':
				m = re.match(r"^\s+updated: ([^;]+); (\d+)s ago,"
				    + r" timeout:\s+(\d+)", line)
				def_router_proposal['updated'] = m.group(1)
				def_router_proposal['ago'] = m.group(2)
				def_router_proposal['timeout'] = m.group(3)
				state = 'DEFAULT_ROUTER'
			elif state == 'RDNS':
				is_id = re.match(r"^\s+id:\s+(\d+), "
				    + r"state:\s+(.+)", line)
				if is_id:
					rdns_proposal = dict();
					rdns_proposal['rdns'] = []
					self.rdns_proposals.append(
					    rdns_proposal)
					rdns_proposal['id'] = is_id.group(1)
					rdns_proposal['state'] = is_id.group(2)
					state = 'RDNS_PROPOSAL'
				else:
					state = 'DONE'
			elif state == 'RDNS_PROPOSAL':
				m = re.match(r"^\s+router: (.+)", line)
				rdns_proposal['router'] = m.group(1)
				state = 'RDNS_PROPOSAL_ROUTER'
			elif state == 'RDNS_PROPOSAL_ROUTER':
				m = re.match(r"^\s+rdns lifetime:\s+(\d)",
				    line)
				rdns_proposal['lifetime'] = m.group(1)
				state = 'RDNS_LIFETIME'
			elif state == 'RDNS_LIFETIME':
				m = re.match(r"^\s+rdns:", line)
				if m:
					state = 'RDNS_RDNS'
			elif state == 'RDNS_RDNS':
				is_upd = re.match(r"^\s+updated: ([^;]+); "
				    + r"(\d+)s ago, timeout:\s+(\d+)", line)
				is_rdns = re.match(r"^\s+([0-9a-fA-F]{1,4}.*)",
				    line)
				if is_upd:
					rdns_proposal['updated'] = \
					    is_upd.group(1)
					rdns_proposal['ago'] = is_upd.group(2)
					rdns_proposal['timeout'] = \
					    is_upd.group(3)
					state = 'DONE'
				elif is_rdns:
					rdns_proposal['rdns'].append(
					    is_rdns.group(1))
					state = 'RDNS_RDNS'
			elif state == 'DONE':
				raise ValueError("got additional data: "
				    + "{0}".format(line))
