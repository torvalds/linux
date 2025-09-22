#	$OpenBSD: Makefile,v 1.30 2021/02/01 12:52:07 bluhm Exp $

# Copyright (c) 2011-2021 Alexander Bluhm <bluhm@openbsd.org>
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

# The following ports must be installed:
#
# scapy               powerful interactive packet manipulation in python

.if ! exists(/usr/local/bin/scapy)
regress:
	@echo Install scapy package to run this regress.
	@echo SKIPPED
.endif

# This test needs a manual setup of four machines
# The setup is the same as for regress/sys/net/pf_forward
# Set up machines: SRC PF RT ECO
# SRC is the machine where this makefile is running.
# PF is running OpenBSD forwarding through pf, it is the test target.
# RT is a router forwarding packets, maximum MTU is 1300.
# ECO is reflecting the ping and UDP echo packets.
# RDR does not exist, PF redirects the traffic to ECO.
# RTT addresses exist on ECO, PF has no route and must use route-to RT
#
# +---+   0   +--+   1   +--+   2   +---+ 7   4 +---+   7 +---+
# |SRC| ----> |PF| ----> |RT| ----> |ECO|       |RDR|     |RTT|
# +---+       +--+       +--+       +---+       +---+     +---+
#     out    in  out    in  out    in   rtt    in        in

# Configure Addresses on the machines, there must be routes for the
# networks.  Adapt interface and address variables to your local
# setup.  To control the remote machine you need a hostname for
# ssh to log in.
# You must have an anchor "regress" for the divert rules in the pf.conf
# of the PF machine.  The kernel of the PF machine gets tested.
#
# Run make check-setup to see if you got the setup correct.

SRC_IF ?=	tap0
SRC_MAC ?=	fe:e1:ba:d1:0a:dc
PF_IFIN ?=	vio0
PF_IFOUT ?=	vio1
PF_MAC ?=	52:54:00:12:34:50
PF_SSH ?=
RT_SSH ?=
ECO_SSH ?=

SRC_OUT ?=	10.188.210.10
PF_IN ?=	10.188.210.50
PF_OUT ?=	10.188.211.50
RT_IN ?=	10.188.211.51
RT_OUT ?=	10.188.212.51
ECO_IN ?=	10.188.212.52
RDR_IN ?=	10.188.214.188
RTT_IN ?=	10.188.217.52

SRC_OUT6 ?=	fdd7:e83e:66bc:210:fce1:baff:fed1:561f
PF_IN6 ?=	fdd7:e83e:66bc:210:5054:ff:fe12:3450
PF_OUT6 ?=	fdd7:e83e:66bc:211:5054:ff:fe12:3450
RT_IN6 ?=	fdd7:e83e:66bc:211:5054:ff:fe12:3451
RT_OUT6 ?=	fdd7:e83e:66bc:212:5054:ff:fe12:3451
ECO_IN6 ?=	fdd7:e83e:66bc:212:5054:ff:fe12:3452
RDR_IN6 ?=	fdd7:e83e:66bc:214::188
RTT_IN6 ?=	fdd7:e83e:66bc:217:5054:ff:fe12:3452

.if empty (PF_SSH) || empty (RT_SSH) || empty (ECO_SSH)
regress:
	@echo this test needs three remote machines to operate on
	@echo PF_SSH RT_SSH ECO_SSH are empty
	@echo fill out these variables for additional tests, then
	@echo check whether your test machines are set up properly
	@echo SKIPPED
.endif

.MAIN: all

.if ! empty (PF_SSH)
.if make (regress) || make (all)
.BEGIN:
	${SUDO} true
	ssh -t ${PF_SSH} ${SUDO} true
	rm -f stamp-pfctl
	@echo
.endif
.endif

# Create python include file containing the addresses.
addr.py: Makefile
	rm -f $@ $@.tmp
	echo 'SRC_IF="${SRC_IF}"' >>$@.tmp
	echo 'SRC_MAC="${SRC_MAC}"' >>$@.tmp
	echo 'PF_IFIN="${PF_IFIN}"' >>$@.tmp
	echo 'PF_IFOUT="${PF_IFOUT}"' >>$@.tmp
	echo 'PF_MAC="${PF_MAC}"' >>$@.tmp
.for var in SRC_OUT PF_IN PF_OUT RT_IN RT_OUT ECO_IN RDR_IN RTT_IN
	echo '${var}="${${var}}"' >>$@.tmp
	echo '${var}6="${${var}6}"' >>$@.tmp
.endfor
	mv $@.tmp $@

# load the pf rules into the kernel of the PF machine
# XXX pfctl does not replace variables after @
REGRESS_SETUP +=	stamp-pfctl
stamp-pfctl: addr.py pf.conf
	cat addr.py ${.CURDIR}/pf.conf | /sbin/pfctl -n -f -
	cat addr.py ${.CURDIR}/pf.conf | \
	    sed 's/@$$PF_IFIN /@${PF_IFIN} /;s/@$$PF_IFOUT /@${PF_IFOUT} /' | \
	    ssh ${PF_SSH} ${SUDO} pfctl -a regress -f -
	@date >$@

# Set variables so that make runs with and without obj directory.
# Only do that if necessary to keep visible output short.
.if ${.CURDIR} == ${.OBJDIR}
PYTHON =	python3 -u ./
.else
PYTHON =	PYTHONPATH=${.OBJDIR} python3 -u ${.CURDIR}/
.endif

# Ping all addresses.  This ensures that the IP addresses are configured
# and all routing table are set up to allow bidirectional packet flow.
# Note that RDR does not exist physically.  So this traffic is rewritten
# by PF and handled by ECO.

REGRESS_TARGETS +=	run-ping
run-ping:
.for ip in SRC_OUT PF_IN PF_OUT RT_IN RT_OUT ECO_IN RDR_IN RTT_IN
	@echo Check ping ${ip}:
	ping -n -c 1 ${${ip}}
.endfor

REGRESS_TARGETS +=	run-ping6
run-ping6:
.for ip in SRC_OUT PF_IN PF_OUT RT_IN RT_OUT ECO_IN RDR_IN RTT_IN
	@echo Check ping ${ip}6:
	ping6 -n -c 1 ${${ip}6}
.endfor

# Ping all addresses again but with 5000 bytes payload.  These large
# packets get fragmented by SRC and must be handled by PF.
# Send 1 packet in advance for Path-MTU discovery.

REGRESS_TARGETS +=	run-fragping
run-fragping:
.for ip in PF_IN PF_OUT RT_IN RT_OUT ECO_IN RDR_IN RTT_IN
	@echo Check ping ${ip}:
	ping -n -c 1 -s 5000 ${${ip}}
.endfor

REGRESS_TARGETS +=	run-fragping6
run-fragping6:
.for ip in PF_IN PF_OUT RT_IN RT_OUT
	@echo Check ping ${ip}6:
	${SUDO} route -n delete -host -inet6 ${${ip}6} || true
	ping6 -n -c 1 -s 1452 -m ${${ip}6} & sleep 1; kill $$! || true
	ping6 -n -c 1 -s 5000 -m ${${ip}6}
.endfor
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping ${ip}6:
	${SUDO} route -n delete -host -inet6 ${${ip}6} || true
	ping6 -n -c 1 -s 1352 -m ${${ip}6} & sleep 1; kill $$! || true
	ping6 -n -c 1 -s 5000 -m ${${ip}6}
.endfor

# Send a large IPv4/ICMP-Echo-Request packet with enabled DF bit and
# parse response packet to determine MTU of the router.  The MTU has
# to be 1300 octets.  The MTU has to be defined at out interface of
# the router RT before.

REGRESS_TARGETS +=	run-ping-mtu
run-ping-mtu: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check path MTU to ${ip}
	${SUDO} ${PYTHON}ping_mtu_1300.py ${${ip}}
.endfor

REGRESS_TARGETS +=	run-ping6-mtu
run-ping6-mtu: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check path MTU to ${ip}6
	${SUDO} ${PYTHON}ping6_mtu_1300.py ${${ip}6}
.endfor

# Send packet too big to get to destination.
# Check that checksum of the quoted original packet in ICMP is correct.

# Currently these test fail as pf does not fix the checksum of
# NATed packets inside of ICMP packets.
# XXX REGRESS_TARGETS +=	run-ping-cksum ping6-cksum run-udp-cksum udp6-cksum

run-ping-cksum: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ICMP chksum in fragmentation needed to ${ip}
	${SUDO} ${PYTHON}ping_cksum.py ${${ip}}
.endfor

run-ping6-cksum: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ICMP6 chksum in packet too big to ${ip}6
	${SUDO} ${PYTHON}ping6_cksum.py ${${ip}6}
.endfor

run-udp-cksum: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check UDP chksum in packet too big to ${ip}
	${SUDO} ${PYTHON}udp_cksum.py ${${ip}}
.endfor

run-udp6-cksum: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check UDP6 chksum in packet too big to ${ip}6
	${SUDO} ${PYTHON}udp6_cksum.py ${${ip}6}
.endfor

# Send handcrafted fragmented packets with overlaps

REGRESS_TARGETS +=	run-frag6
run-frag: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping reassembly ${ip}
	${SUDO} ${PYTHON}frag.py ${${ip}}
.endfor

REGRESS_TARGETS +=	run-frag
run-frag6: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping6 reassembly ${ip}6
	${SUDO} ${PYTHON}frag6.py ${${ip}6}
.endfor

REGRESS_TARGETS +=	run-frag6-ext
run-frag6-ext: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping6 extension header reassembly ${ip}6
	${SUDO} ${PYTHON}frag6_ext.py ${${ip}6}
.endfor

REGRESS_TARGETS +=	run-frag-cutnew
run-frag-cutnew: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping head overlap reassembly ${ip}
	${SUDO} ${PYTHON}frag_cutnew.py ${${ip}}
.endfor

REGRESS_TARGETS +=	run-frag6-cutnew
run-frag6-cutnew: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping6 head overlap reassembly ${ip}6
	${SUDO} ${PYTHON}frag6_cutnew.py ${${ip}6}
.endfor

REGRESS_TARGETS +=	run-frag-cutold
run-frag-cutold: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping tail overlap reassembly ${ip}
	${SUDO} ${PYTHON}frag_cutold.py ${${ip}}
.endfor

REGRESS_TARGETS +=	run-frag6-cutold
run-frag6-cutold: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping6 tail overlap reassembly ${ip}6
	${SUDO} ${PYTHON}frag6_cutold.py ${${ip}6}
.endfor

REGRESS_TARGETS +=	run-frag-dropold
run-frag-dropold: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping total overlapping reassembly ${ip}
	${SUDO} ${PYTHON}frag_dropold.py ${${ip}}
.endfor

REGRESS_TARGETS +=	run-frag6-dropold
run-frag6-dropold: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping6 total overlapping reassembly ${ip}6
	${SUDO} ${PYTHON}frag6_dropold.py ${${ip}6}
.endfor

REGRESS_TARGETS +=	run-frag-dropnew
run-frag-dropnew: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping total overlaped reassembly ${ip}
	${SUDO} ${PYTHON}frag_dropnew.py ${${ip}}
.endfor

REGRESS_TARGETS +=	run-frag6-dropnew
run-frag6-dropnew: addr.py
.for ip in ECO_IN RDR_IN RTT_IN
	@echo Check ping6 total overlaped reassembly ${ip}6
	${SUDO} ${PYTHON}frag6_dropnew.py ${${ip}6}
.endfor

CLEANFILES +=		addr.py *.pyc *.log stamp-*

.PHONY: check-setup

# Check whether the address, route and remote setup are correct
check-setup: check-setup-src check-setup-pf check-setup-rt check-setup-eco

check-setup-src:
	@echo '\n======== $@ ========'
.for ip in SRC_OUT
	ping -n -c 1 ${${ip}}  # ${ip}
	route -n get -inet ${${ip}} | grep -q 'flags: .*LOCAL'  # ${ip}
.endfor
	ping -n -c 1 ${PF_IN}  # PF_IN
	route -n get -inet ${PF_IN} | fgrep -q 'interface: ${SRC_IF}'  # PF_IN SRC_IF
.for ip in PF_OUT RT_IN RT_OUT ECO_IN RDR_IN RTT_IN
	route -n get -inet ${${ip}} | fgrep -q 'gateway: ${PF_IN}'  # ${ip} PF_IN
.endfor
.for ip in SRC_OUT
	ping6 -n -c 1 ${${ip}6}  # ${ip}6
	route -n get -inet6 ${${ip}6} | grep -q 'flags: .*LOCAL'  # ${ip}6
.endfor
	ping6 -n -c 1 ${PF_IN6}  # PF_IN6
	route -n get -inet6 ${PF_IN6} | fgrep -q 'interface: ${SRC_IF}'  # PF_IN6 SRC_IF
.for ip in PF_OUT RT_IN RT_OUT ECO_IN RDR_IN RTT_IN
	route -n get -inet6 ${${ip}6} | fgrep -q 'gateway: ${PF_IN6}'  # ${ip}6 PF_IN6
.endfor

check-setup-pf:
	@echo '\n======== $@ ========'
	ssh ${PF_SSH} ping -n -c 1 ${PF_IN}  # PF_IN
	ssh ${PF_SSH} route -n get -inet ${PF_IN} | grep -q 'flags: .*LOCAL'  # PF_IN
	ssh ${PF_SSH} ping -n -c 1 ${SRC_OUT}  # SRC_OUT
	ssh ${PF_SSH} ping -n -c 1 ${PF_OUT}  # PF_OUT
	ssh ${PF_SSH} route -n get -inet ${PF_OUT} | grep -q 'flags: .*LOCAL'  # PF_OUT
	ssh ${PF_SSH} ping -n -c 1 ${RT_IN}  # RT_IN
.for ip in RT_OUT ECO_IN
	ssh ${PF_SSH} route -n get -inet ${${ip}} | fgrep -q 'gateway: ${RT_IN}'  # ${ip} RT_IN
.endfor
.for ip in RTT_IN
	ssh ${PF_SSH} route -n get -inet ${${ip}} | grep -q 'flags: .*REJECT'  # ${ip} reject
.endfor
	ssh ${PF_SSH} ping6 -n -c 1 ${PF_IN6}  # PF_IN6
	ssh ${PF_SSH} route -n get -inet6 ${PF_IN6} | grep -q 'flags: .*LOCAL'  # PF_IN6
	ssh ${PF_SSH} ping6 -n -c 1 ${SRC_OUT6}  # SRC_OUT6
	ssh ${PF_SSH} ping6 -n -c 1 ${PF_OUT6}  # PF_OUT6
	ssh ${PF_SSH} route -n get -inet6 ${PF_OUT6} | grep -q 'flags: .*LOCAL'  # PF_OUT6
	ssh ${PF_SSH} ping6 -n -c 1 ${RT_IN6}  # RT_IN6
.for ip in RT_OUT ECO_IN
	ssh ${PF_SSH} route -n get -inet6 ${${ip}6} | fgrep -q 'gateway: ${RT_IN6}'  # ${ip}6 RT_IN6
.endfor
.for ip in RTT_IN
	ssh ${PF_SSH} route -n get -inet6 ${${ip}6} | grep -q 'flags: .*REJECT'  # ${ip}6 reject
.endfor
	ssh ${PF_SSH} ${SUDO} pfctl -sr | grep '^anchor "regress" all$$'
	ssh ${PF_SSH} ${SUDO} pfctl -si | grep '^Status: Enabled '
	ssh ${PF_SSH} sysctl net.inet.ip.forwarding | fgrep =1
	ssh ${PF_SSH} sysctl net.inet6.ip6.forwarding | fgrep =1

check-setup-rt:
	@echo '\n======== $@ ========'
	ssh ${RT_SSH} ping -n -c 1 ${RT_IN}  # RT_IN
	ssh ${RT_SSH} route -n get -inet ${RT_IN} | grep -q 'flags: .*LOCAL'  # RT_IN
	ssh ${RT_SSH} ping -n -c 1 ${PF_OUT}  # PF_OUT
.for ip in PF_IN SRC_OUT
	ssh ${RT_SSH} route -n get -inet ${${ip}} | fgrep -q 'gateway: ${PF_OUT}'  # ${ip} PF_OUT
.endfor
	ssh ${RT_SSH} ping -n -c 1 ${RT_OUT}  # RT_OUT
	ssh ${RT_SSH} route -n get -inet ${RT_OUT} | grep -q 'flags: .*LOCAL'  # RT_OUT
	ssh ${RT_SSH} ping -n -c 1 ${ECO_IN}  # ECO_IN
.for ip in RTT_IN
	ssh ${RT_SSH} route -n get -inet ${${ip}} | fgrep -q 'gateway: ${ECO_IN}'  # ${ip} ECO_IN
.endfor
	ssh ${RT_SSH} ping6 -n -c 1 ${RT_IN6}  # RT_IN6
	ssh ${RT_SSH} route -n get -inet6 ${RT_IN6} | grep -q 'flags: .*LOCAL'  # RT_IN6
	ssh ${RT_SSH} ping6 -n -c 1 ${PF_OUT6}  # PF_OUT6
.for ip in PF_IN SRC_OUT
	ssh ${RT_SSH} route -n get -inet6 ${${ip}6} | fgrep -q 'gateway: ${PF_OUT6}'  # ${ip}6 PF_OUT6
.endfor
	ssh ${RT_SSH} ping6 -n -c 1 ${RT_OUT6}  # RT_OUT6
	ssh ${RT_SSH} route -n get -inet6 ${RT_OUT6} | grep -q 'flags: .*LOCAL'  # RT_OUT6
	ssh ${RT_SSH} ping6 -n -c 1 ${ECO_IN6}  # ECO_IN6
.for ip in RTT_IN
	ssh ${RT_SSH} route -n get -inet6 ${${ip}6} | fgrep -q 'gateway: ${ECO_IN6}'  # ${ip}6 ECO_IN6
.endfor
	ssh ${RT_SSH} sysctl net.inet.ip.forwarding | fgrep =1
	ssh ${RT_SSH} sysctl net.inet6.ip6.forwarding | fgrep =1
	ssh ${RT_SSH} ifconfig | fgrep 'mtu 1300'

check-setup-eco:
	@echo '\n======== $@ ========'
.for ip in ECO_IN RTT_IN
	ssh ${ECO_SSH} ping -n -c 1 ${${ip}}  # ${ip}
	ssh ${ECO_SSH} route -n get -inet ${${ip}} | grep -q 'flags: .*LOCAL'  # ${ip}
.endfor
	ssh ${ECO_SSH} ping -n -c 1 ${RT_OUT}  # RT_OUT
.for ip in RT_IN PF_OUT PF_IN SRC_OUT
	ssh ${ECO_SSH} route -n get -inet ${${ip}} | fgrep -q 'gateway: ${RT_OUT}'  # ${ip} RT_OUT
.endfor
.for ip in ECO_IN RTT_IN
	ssh ${ECO_SSH} ping6 -n -c 1 ${${ip}6}  # ${ip}6
	ssh ${ECO_SSH} route -n get -inet6 ${${ip}6} | grep -q 'flags: .*LOCAL'  # ${ip}6
.endfor
	ssh ${ECO_SSH} ping6 -n -c 1 ${RT_OUT6}  # RT_OUT6
.for ip in RT_IN PF_OUT PF_IN SRC_OUT
	ssh ${ECO_SSH} route -n get -inet6 ${${ip}6} | fgrep -q 'gateway: ${RT_OUT6}'  # ${ip}6 RT_OUT6
	ssh ${ECO_SSH} sysctl net.inet.ip.forwarding | fgrep =1
	ssh ${ECO_SSH} sysctl net.inet6.ip6.forwarding | fgrep =1
.endfor

.include <bsd.regress.mk>
