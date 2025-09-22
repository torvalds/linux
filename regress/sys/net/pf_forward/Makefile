#	$OpenBSD: Makefile,v 1.36 2025/08/03 12:43:38 sashan Exp $

# Copyright (c) 2012-2021 Alexander Bluhm <bluhm@openbsd.org>
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
# The setup is the same as for regress/sys/net/pf_fragment
# Set up machines: SRC PF RT ECO
# SRC is the machine where this makefile is running.
# PF is running OpenBSD forwarding through pf, it is the test target.
# RT is a router forwarding packets, maximum MTU is 1300.
# ECO is reflecting the ping and UDP and TCP echo packets.
# RDR does not exist, PF redirects the traffic to ECO.
# AF does not exist, PF translates address family and sends to ECO.
# RTT addresses exist on ECO, PF has no route and must use route-to RT
# RPT addresses exist on SRC, PF has no route and must use reply-to SRC
#
# +---+   1   +--+   2   +--+   3   +---+ 4
# |SRC| ----> |PF| ----> |RT| ----> |ECO| 7
# +---+ 8     +--+       +--+       +---+ 9
#     out    in  out    in  out    in   out
#
# 5 +---+ 6   7 +--+    8 +---+ 9   10 +---+ 11
#   |RDR|       |AF|      |RTT|        |RPT|
#   +---+       +--+      +---+        +---+
#  in   out    in        in          out

# Configure Addresses on the machines, there must be routes for the
# networks.  Adapt interface and addresse variables to your local
# setup.  To control the remote machine you need a hostname for
# ssh to log in.
# You must have an anchor "regress" for the divert rules in the pf.conf
# of the PF machine.  The kernel of the PF machine gets testet.
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
ECO_OUT ?=	10.188.213.52
RDR_IN ?=	10.188.214.188
RDR_OUT ?=	10.188.215.188
AF_IN ?=	10.188.216.82		# /24 must be dec(ECO_IN6/120)
RTT_IN ?=	10.188.217.52
RTT_OUT ?=	10.188.218.52
RPT_IN ?=	10.188.220.10
RPT_OUT ?=	10.188.221.10

SRC_OUT6 ?=	fdd7:e83e:66bc:210:fce1:baff:fed1:561f
PF_IN6 ?=	fdd7:e83e:66bc:210:5054:ff:fe12:3450
PF_OUT6 ?=	fdd7:e83e:66bc:211:5054:ff:fe12:3450
RT_IN6 ?=	fdd7:e83e:66bc:211:5054:ff:fe12:3451
RT_OUT6 ?=	fdd7:e83e:66bc:212:5054:ff:fe12:3451
ECO_IN6 ?=	fdd7:e83e:66bc:212:5054:ff:fe12:3452
ECO_OUT6 ?=	fdd7:e83e:66bc:213:5054:ff:fe12:3452
RDR_IN6 ?=	fdd7:e83e:66bc:214::188
RDR_OUT6 ?=	fdd7:e83e:66bc:215::188
AF_IN6 ?=	fdd7:e83e:66bc:216::34	# /120 must be hex(ECO_IN/24)
RTT_IN6 ?=	fdd7:e83e:66bc:217:5054:ff:fe12:3452
RTT_OUT6 ?=	fdd7:e83e:66bc:218:5054:ff:fe12:3452
RPT_IN6 ?=	fdd7:e83e:66bc:1220:fce1:baff:fed1:561f
RPT_OUT6 ?=	fdd7:e83e:66bc:1221:fce1:baff:fed1:561f

.if empty (PF_SSH) || empty (RT_SSH) || empty (ECO_SSH)
regress:
	@echo this tests needs three remote machines to operate on
	@echo PF_SSH RT_SSH ECO_SSH are empty
	@echo fill out these variables for additional tests, then
	@echo check wether your test machines are set up properly
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
.for var in SRC_OUT PF_IN PF_OUT RT_IN RT_OUT ECO_IN ECO_OUT RDR_IN RDR_OUT\
    AF_IN RTT_IN RTT_OUT RPT_IN RPT_OUT
	echo '${var}="${${var}}"' >>$@.tmp
	echo '${var}6="${${var}6}"' >>$@.tmp
.endfor
	mv $@.tmp $@

# Load the pf rules into the kernel of the PF machine.
# XXX pfctl does not replace variables after @.
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

.for inet in inet inet6

run-ping-mtu-1400-${inet}-RPT_OUT:
	# RPT_OUT with locally generated ICMP time exceeded cannot work.
	# The generated packet will not match the out rule with reply-to
	# so it will be rejected by the route.
	@echo DISABLED

.for proto in icmp udp
run-traceroute-${proto}-${inet}-RPT_OUT:
	# RPT_OUT traceroute cannot work.  The ICMP time exceeded packet
	# generated by IP forward will not match the out rule with reply-to
	# so it will be rejected by the route.
	@echo DISABLED
.endfor # proto

# Ping all addresses.  This ensures that the IP addresses are configured
# and all routing table are set up to allow bidirectional packet flow.
# Note that RDR does not exist physically.  So this traffic is rewritten
# by PF and handled by ECO.

.for ip in SRC_OUT PF_IN PF_OUT RT_IN RT_OUT ECO_IN ECO_OUT RDR_IN RDR_OUT\
    AF_IN RTT_IN RTT_OUT RPT_IN RPT_OUT
REGRESS_TARGETS +=	run-ping-${inet}-${ip}
run-ping-${inet}-${ip}: stamp-pfctl
	@echo Check ping ${ip}${inet:S/inet//}:
.if "RPT_IN" == ${ip} || "RPT_OUT" == ${ip}
	ping${inet:S/inet//} -n -c 1 -I ${${ip}${inet:S/inet//}}\
	    ${ECO_IN${inet:S/inet//}}
.else
	ping${inet:S/inet//} -n -c 1 ${${ip}${inet:S/inet//}}
.endif
.endfor # ip

.for ip in ECO_IN ECO_OUT RDR_IN RDR_OUT AF_IN RTT_IN RTT_OUT RPT_IN RPT_OUT

# Send a large IPv4/ICMP-Echo-Request packet with enabled DF bit and
# parse response packet to determine MTU of the packet filter.  The
# outgoing MTU of PF has to be 1400 octets.  Packet size is 1500.
# Check that the IP length of the original packet and the ICMP
# quoted packet are the same.

REGRESS_TARGETS +=	run-ping-mtu-1400-${inet}-${ip}
run-ping-mtu-1400-${inet}-${ip}: stamp-pfctl
	@echo Check path MTU to ${ip}${inet:S/inet//} is 1400
.if "RPT_IN" == ${ip} || "RPT_OUT" == ${ip}
	${SUDO} ${PYTHON}ping${inet:S/inet//}_mtu.py ${${ip}${inet:S/inet//}}\
	    ${ECO_IN${inet:S/inet//}} 1500 1400
.elif "AF_IN" == ${ip}
.if "inet" == ${inet}
	${SUDO} ${PYTHON}ping_mtu.py ${SRC_OUT} ${${ip}} 1500 1380
.else
	${SUDO} ${PYTHON}ping6_mtu.py ${SRC_OUT6} ${${ip}6} 1500 1420
.endif
.else
	${SUDO} ${PYTHON}ping${inet:S/inet//}_mtu.py ${SRC_OUT${inet:S/inet//}}\
	    ${${ip}${inet:S/inet//}} 1500 1400
.endif

# Send a large IPv4/ICMP-Echo-Request packet with enabled DF bit and
# parse response packet to determine MTU of the router.  The MTU has
# to be 1300 octets.  The MTU has to be defined at out interface of
# the router RT before.  Packet size is 1400 to pass PF MTU.
# Check that the IP length of the original packet and the ICMP
# quoted packet are the same.

REGRESS_TARGETS +=	run-ping-mtu-1300-${inet}-${ip}
run-ping-mtu-1300-${inet}-${ip}: stamp-pfctl
	@echo Check path MTU from ${ip}${inet:S/inet//} is 1300
.if "RPT_IN" == ${ip} || "RPT_OUT" == ${ip}
	${SUDO} ${PYTHON}ping${inet:S/inet//}_mtu.py ${${ip}${inet:S/inet//}}\
	    ${ECO_IN${inet:S/inet//}} 1400 1300
.elif "AF_IN" == ${ip}
.if "inet" == ${inet}
	${SUDO} ${PYTHON}ping_mtu.py ${SRC_OUT} ${${ip}} 1380 1280
.else
	${SUDO} ${PYTHON}ping6_mtu.py ${SRC_OUT6} ${${ip}6} 1420 1320
.endif
.else
	${SUDO} ${PYTHON}ping${inet:S/inet//}_mtu.py ${SRC_OUT${inet:S/inet//}}\
	    ${${ip}${inet:S/inet//}} 1400 1300
.endif

# Send one UDP echo port 7 packet to all destination addresses with netcat.
# The response must arrive in 1 second.

REGRESS_TARGETS +=	run-udp-${inet}-${ip}
run-udp-${inet}-${ip}: stamp-pfctl
	@echo Check UDP ${ip${inet:S/inet//}}:
.if "RPT_IN" == ${ip} || "RPT_OUT" == ${ip}
	echo $$$$ | nc -n -u -W 1 -w 3 -s ${${ip}${inet:S/inet//}}\
	    ${ECO_IN${inet:S/inet//}} 7 | grep $$$$
.else
	echo $$$$ | nc -n -u -W 1 -w 3 ${${ip}${inet:S/inet//}} 7 | grep $$$$
.endif

# Send a data stream to TCP echo port 7 to all destination addresses
# with netcat.  Use enough data to make sure PMTU discovery works.
# Count the reflected bytes and compare with the transmitted ones.
# Delete host route before test to trigger PMTU discovery.

REGRESS_TARGETS +=	run-tcp-${inet}-${ip}
run-tcp-${inet}-${ip}: stamp-pfctl
	@echo Check tcp ${ip}${inet:S/inet//}:
	${SUDO} route -n delete -host -inet ${${ip}${inet:S/inet//}} || true
.if "RPT_IN" == ${ip} || "RPT_OUT" == ${ip}
	openssl rand 200000 | nc -n -N -w 10 -s ${${ip}${inet:S/inet//}}\
	    ${ECO_IN${inet:S/inet//}} 7 | wc -c | grep '200000$$'
.else
	openssl rand 200000 | nc -n -N -w 10 ${${ip}${inet:S/inet//}} 7 |\
	    wc -c | grep '200000$$'
.endif

.endfor # ip

# Run traceroute with ICMP and UDP to all destination addresses.
# Expect three hops in output and that every probe has a response.

TRACEROUTE_CHECK =	awk \
    'BEGIN{ x=0 } \
    { print $$0 } \
    { n=$$1 } \
    /\*/{ x++ } \
    END{ if (n!=3) { print "hopcount is not 3: "n; exit 1 } } \
    END{ if (x!=0) { print "unanswered probes: "x; exit 1 } }'

.for ip in ECO_IN ECO_OUT RDR_IN RDR_OUT AF_IN RTT_IN RTT_OUT RPT_IN RPT_OUT
.for proto in icmp udp
REGRESS_TARGETS +=	run-traceroute-${proto}-${inet}-${ip}
run-traceroute-${proto}-${inet}-${ip}: stamp-pfctl
	@echo Check traceroute ${proto} ${ip${inet:S/inet//}}:
.if "RPT_IN" == ${ip} || "RPT_OUT" == ${ip}
	traceroute${inet:S/inet//} ${proto:S/icmp/-I/:S/udp//}\
	    -s ${${ip}${inet:S/inet//}} ${ECO_IN${inet:S/inet//}} |\
	    ${TRACEROUTE_CHECK}
.else
	traceroute${inet:S/inet//} ${proto:S/icmp/-I/:S/udp//}\
	    ${${ip}${inet:S/inet//}} | ${TRACEROUTE_CHECK}
.endif
.endfor # proto
.endfor # ip

.endfor # inet

CLEANFILES +=		addr.py *.pyc *.log stamp-*

.PHONY: check-setup

# Check wether the address, route and remote setup is correct
check-setup: check-setup-src check-setup-pf check-setup-rt check-setup-eco

check-setup-src:
	@echo '\n======== $@ ========'
.for ip in SRC_OUT RPT_IN RPT_OUT
	ping -n -c 1 ${${ip}}  # ${ip}
	/sbin/route -n get -inet ${${ip}} | grep -q 'flags: .*LOCAL'  # ${ip}
.endfor
	ping -n -c 1 ${PF_IN}  # PF_IN
	/sbin/route -n get -inet ${PF_IN} | fgrep -q 'interface: ${SRC_IF}' \
	    # PF_IN SRC_IF
.for ip in PF_OUT RT_IN RT_OUT ECO_IN ECO_OUT RDR_IN RDR_OUT AF_IN\
    RTT_IN RTT_OUT
	/sbin/route -n get -inet ${${ip}} | fgrep -q 'gateway: ${PF_IN}' \
	    # ${ip} PF_IN
.endfor
.for ip in SRC_OUT RPT_IN RPT_OUT
	ping6 -n -c 1 ${${ip}6}  # ${ip}6
	/sbin/route -n get -inet6 ${${ip}6} | grep -q 'flags: .*LOCAL'  # ${ip}6
.endfor
	ping6 -n -c 1 ${PF_IN6}  # PF_IN6
	/sbin/route -n get -inet6 ${PF_IN6} | fgrep -q 'interface: ${SRC_IF}' \
	    # PF_IN6 SRC_IF
.for ip in PF_OUT RT_IN RT_OUT ECO_IN ECO_OUT RDR_IN RDR_OUT AF_IN\
    RTT_IN RTT_OUT
	/sbin/route -n get -inet6 ${${ip}6} | fgrep -q 'gateway: ${PF_IN6}' \
	    # ${ip}6 PF_IN6
.endfor
	/sbin/sysctl net.inet.ip.forwarding | fgrep =1
	/sbin/sysctl net.inet6.ip6.forwarding | fgrep =1

check-setup-pf:
	@echo '\n======== $@ ========'
	ssh ${PF_SSH} ping -n -c 1 ${PF_IN}  # PF_IN
	ssh ${PF_SSH} route -n get -inet ${PF_IN} | grep -q 'flags: .*LOCAL' \
	    # PF_IN
	ssh ${PF_SSH} ping -n -c 1 ${SRC_OUT}  # SRC_OUT
	ssh ${PF_SSH} ping -n -c 1 ${PF_OUT}  # PF_OUT
	ssh ${PF_SSH} route -n get -inet ${PF_OUT} | grep -q 'flags: .*LOCAL' \
	    # PF_OUT
	ssh ${PF_SSH} ping -n -c 1 ${RT_IN}  # RT_IN
.for ip in RT_OUT ECO_IN ECO_OUT
	ssh ${PF_SSH} route -n get -inet ${${ip}} |\
	    fgrep -q 'gateway: ${RT_IN}'  # ${ip} RT_IN
.endfor
.for ip in RTT_IN RTT_OUT RPT_IN RPT_OUT
	ssh ${PF_SSH} route -n get -inet ${${ip}} | grep -q 'flags: .*REJECT' \
	    # ${ip} reject
.endfor
	ssh ${PF_SSH} ping6 -n -c 1 ${PF_IN6}  # PF_IN6
	ssh ${PF_SSH} route -n get -inet6 ${PF_IN6} | grep -q 'flags: .*LOCAL' \
	    # PF_IN6
	ssh ${PF_SSH} ping6 -n -c 1 ${SRC_OUT6}  # SRC_OUT6
	ssh ${PF_SSH} ping6 -n -c 1 ${PF_OUT6}  # PF_OUT6
	ssh ${PF_SSH} route -n get -inet6 ${PF_OUT6} |\
	    grep -q 'flags: .*LOCAL'  # PF_OUT6
	ssh ${PF_SSH} ping6 -n -c 1 ${RT_IN6}  # RT_IN6
.for ip in RT_OUT ECO_IN ECO_OUT
	ssh ${PF_SSH} route -n get -inet6 ${${ip}6} |\
	    fgrep -q 'gateway: ${RT_IN6}'  # ${ip}6 RT_IN6
.endfor
.for ip in RTT_IN RTT_OUT RPT_IN RPT_OUT
	ssh ${PF_SSH} route -n get -inet6 ${${ip}6} |\
	    grep -q 'flags: .*REJECT'  # ${ip}6 reject
.endfor
	ssh ${PF_SSH} ${SUDO} pfctl -sr | grep '^anchor "regress" all$$'
	ssh ${PF_SSH} ${SUDO} pfctl -si | grep '^Status: Enabled '
	# 'set skip lo' rule must be removed/commented out
	ssh ${PF_SSH} ${SUDO} pfctl -sI -v | egrep '^lo.?' | grep -v skip
	ssh ${PF_SSH} sysctl net.inet.ip.forwarding | fgrep =1
	ssh ${PF_SSH} sysctl net.inet6.ip6.forwarding | fgrep =1
	ssh ${PF_SSH} ifconfig ${PF_IFOUT} | fgrep 'mtu 1400'

check-setup-rt:
	@echo '\n======== $@ ========'
	ssh ${RT_SSH} ping -n -c 1 ${RT_IN}  # RT_IN
	ssh ${RT_SSH} route -n get -inet ${RT_IN} | grep -q 'flags: .*LOCAL' \
	    # RT_IN
	ssh ${RT_SSH} ping -n -c 1 ${PF_OUT}  # PF_OUT
.for ip in PF_IN SRC_OUT RPT_IN RPT_OUT
	ssh ${RT_SSH} route -n get -inet ${${ip}} |\
	    fgrep -q 'gateway: ${PF_OUT}'  # ${ip} PF_OUT
.endfor
	ssh ${RT_SSH} ping -n -c 1 ${RT_OUT}  # RT_OUT
	ssh ${RT_SSH} route -n get -inet ${RT_OUT} | grep -q 'flags: .*LOCAL' \
	    # RT_OUT
	ssh ${RT_SSH} ping -n -c 1 ${ECO_IN}  # ECO_IN
.for ip in ECO_OUT RTT_IN RTT_OUT
	ssh ${RT_SSH} route -n get -inet ${${ip}} |\
	    fgrep -q 'gateway: ${ECO_IN}'  # ${ip} ECO_IN
.endfor
	ssh ${RT_SSH} ping6 -n -c 1 ${RT_IN6}  # RT_IN6
	ssh ${RT_SSH} route -n get -inet6 ${RT_IN6} | grep -q 'flags: .*LOCAL' \
	    # RT_IN6
	ssh ${RT_SSH} ping6 -n -c 1 ${PF_OUT6}  # PF_OUT6
.for ip in PF_IN SRC_OUT RPT_IN RPT_OUT
	ssh ${RT_SSH} route -n get -inet6 ${${ip}6} |\
	    fgrep -q 'gateway: ${PF_OUT6}'  # ${ip}6 PF_OUT6
.endfor
	ssh ${RT_SSH} ping6 -n -c 1 ${RT_OUT6}  # RT_OUT6
	ssh ${RT_SSH} route -n get -inet6 ${RT_OUT6} |\
	    grep -q 'flags: .*LOCAL'  # RT_OUT6
	ssh ${RT_SSH} ping6 -n -c 1 ${ECO_IN6}  # ECO_IN6
.for ip in ECO_OUT RTT_IN RTT_OUT
	ssh ${RT_SSH} route -n get -inet6 ${${ip}6} |\
	    fgrep -q 'gateway: ${ECO_IN6}'  # ${ip}6 ECO_IN6
.endfor
	ssh ${RT_SSH} sysctl net.inet.ip.forwarding | fgrep =1
	ssh ${RT_SSH} sysctl net.inet6.ip6.forwarding | fgrep =1
	ssh ${RT_SSH} ifconfig | fgrep 'mtu 1300'

check-setup-eco:
	@echo '\n======== $@ ========'
.for ip in ECO_IN ECO_OUT RTT_IN RTT_OUT
	ssh ${ECO_SSH} ping -n -c 1 ${${ip}}  # ${ip}
	ssh ${ECO_SSH} route -n get -inet ${${ip}} | grep -q 'flags: .*LOCAL' \
	    # ${ip}
.endfor
	ssh ${ECO_SSH} ping -n -c 1 ${RT_OUT}  # RT_OUT
.for ip in RT_IN PF_OUT PF_IN SRC_OUT RPT_IN RPT_OUT
	ssh ${ECO_SSH} route -n get -inet ${${ip}} |\
	    fgrep -q 'gateway: ${RT_OUT}'  # ${ip} RT_OUT
.endfor
.for ip in ECO_IN ECO_OUT RTT_IN RTT_OUT
	ssh ${ECO_SSH} ping6 -n -c 1 ${${ip}6}  # ${ip}6
	ssh ${ECO_SSH} route -n get -inet6 ${${ip}6} |\
	    grep -q 'flags: .*LOCAL'  # ${ip}6
.endfor
	ssh ${ECO_SSH} ping6 -n -c 1 ${RT_OUT6}  # RT_OUT6
.for ip in RT_IN PF_OUT PF_IN SRC_OUT RPT_IN RPT_OUT
	ssh ${ECO_SSH} route -n get -inet6 ${${ip}6} |\
	    fgrep -q 'gateway: ${RT_OUT6}'  # ${ip}6 RT_OUT6
.endfor
.for inet in inet inet6
.for proto in udp tcp
	ssh ${ECO_SSH} netstat -na -f ${inet} -p ${proto} | fgrep ' *.7 '
.endfor
.endfor
.for ip in ECO_IN ECO_OUT RTT_IN RTT_OUT
	ssh ${ECO_SSH} netstat -nav -f inet -p udp | fgrep ' ${${ip}}.7 '
	ssh ${ECO_SSH} netstat -nav -f inet6 -p udp | fgrep ' ${${ip}6}.7 '
	ssh ${ECO_SSH} sysctl net.inet.ip.forwarding | fgrep =1
	ssh ${ECO_SSH} sysctl net.inet6.ip6.forwarding | fgrep =1
.endfor

.include <bsd.regress.mk>
