# $OpenBSD: Makefile,v 1.7 2023/10/11 18:07:56 anton Exp $

# Copyright (c) 2022 Alexander Bluhm <bluhm@openbsd.org>
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

# Set up two loopback interfaces in different routing domains.
# One loopback interface has a allow-opts pf rule, the other has
# default pass policy.  Send packets with IP options and IPv6
# option header and check wheter tcpdump finds them on lo or pflog.

# The following ports must be installed:
#
# scapy			powerful interactive packet manipulation in python

.if ! exists(/usr/local/bin/scapy)
regress:
	@echo Install scapy package to run this regress.
	@echo SKIPPED
.endif

# This test uses routing domain and interface number 11 and 12.
# Adjust it here, if you want to use something else.
N1 =		11
N2 =		12
NUMS =		${N1} ${N2}

.include <bsd.own.mk>

.if ! (make(clean) || make(cleandir) || make(obj))

PF_STATUS !=	${SUDO} /sbin/pfctl -si | sed -n 's/^Status: \([^ ]*\) .*/\1/p'
.if empty(PF_STATUS:MEnabled)
regress:
	@echo pf status: "${PF_STATUS}"
	@echo Enable pf to run this regress.
	@echo SKIPPED
.endif

PF_SKIP !=	${SUDO} /sbin/pfctl -sI -v | sed -n 's/ (skip)//p' | \
		grep -w -e lo${N1} -e lo${N2} || :
.if ! empty(PF_SKIP)
regress:
	@echo pf skip: "${PF_SKIP}"
	@echo Do not set skip on interface lo, lo${N1}, or lo${N2}.
	@echo SKIPPED
.endif

PF_ANCHOR !=	${SUDO} /sbin/pfctl -sr |\
		    sed -n 's/^anchor "\([^"]*\)" all$$/\1/p'
.if empty(PF_ANCHOR:Mregress)
regress:
	@echo pf anchor: "${PF_ANCHOR}"
	@echo Need anchor '"regress"' in pf.conf to load additional rules.
	@echo SKIPPED
.endif

.endif

.PHONY: busy-rdomains ifconfig unconfig pfctl

REGRESS_SETUP_ONCE +=	busy-rdomains
busy-rdomains:
	# Check if rdomains are busy.
.for n in ${NUMS}
	@if /sbin/ifconfig | grep -v '^lo$n:' | grep ' rdomain $n '; then\
	    echo routing domain $n is already used >&2; exit 1; fi
.endfor

REGRESS_SETUP_ONCE +=	ifconfig
ifconfig: unconfig
	# Create and configure loopback interfaces.
.for n in ${NUMS}
	${SUDO} /sbin/ifconfig lo$n rdomain $n
	${SUDO} /sbin/ifconfig lo$n inet 127.0.0.1/8
	${SUDO} /sbin/ifconfig lo$n inet 127.0.0.$n alias
	${SUDO} /sbin/ifconfig lo$n inet6 ::1/128
	${SUDO} /sbin/ifconfig lo$n inet6 fe80::$n/64
	${SUDO} /sbin/route -n -T $n add -inet 224.0.0.0/4 127.0.0.1
.endfor
	# Wait until IPv6 addresses are no longer tentative.
	for i in `jot 50`; do\
	    if ! { /sbin/ifconfig lo${N1}; /sbin/ifconfig lo${N2}; }\
		| fgrep -q tentative; then\
		    break;\
	    fi;\
	    sleep .1;\
	done
	! { /sbin/ifconfig lo${N1}; /sbin/ifconfig lo${N2}; }\
	    | fgrep tentative

REGRESS_CLEANUP +=	unconfig
unconfig: stamp-stop
	# Destroy interfaces.
.for n in ${NUMS}
	-${SUDO} /sbin/ifconfig lo$n rdomain $n
	-${SUDO} /sbin/ifconfig lo$n inet 127.0.0.1 delete
	-${SUDO} /sbin/ifconfig lo$n inet 127.0.0.$n delete
	-${SUDO} /sbin/ifconfig lo$n inet6 ::1 delete
	-${SUDO} /sbin/ifconfig lo$n inet6 fe80::$n/64 delete
.endfor
	rm -f stamp-ifconfig

addr.py: Makefile
	# Create python include file containing the addresses.
	rm -f $@ $@.tmp
.for var in N1 N2
	echo '${var}="${${var}}"' >>$@.tmp
	echo 'IF_${var}="lo${${var}}"' >>$@.tmp
	echo 'ADDR_${var}="127.0.0.${${var}}"' >>$@.tmp
	echo 'ADDR6_${var}="fe80::${${var}}"' >>$@.tmp
.endfor
	mv $@.tmp $@

REGRESS_SETUP_ONCE +=	pfctl
pfctl: addr.py pf.conf
	# Load the pf rules into the kernel.
	cat addr.py ${.CURDIR}/pf.conf | /sbin/pfctl -n -f -
	cat addr.py ${.CURDIR}/pf.conf | ${SUDO} /sbin/pfctl -a regress -f -

# run tcpdump on lo and pflog device
DUMPCMD =	/usr/sbin/tcpdump -l -e -vvv -s 2048 -ni

stamp-bpf: stamp-bpf-lo${N1} stamp-bpf-lo${N2} stamp-bpf-pflog0
	sleep 2  # XXX
	@date >$@

.for i in lo${N1} lo${N2} pflog0

stamp-bpf-$i: stamp-ifconfig
	rm -f $i.tcpdump
	${SUDO} pkill -f '^${DUMPCMD} $i' || true
	${SUDO} ${DUMPCMD} $i >$i.tcpdump &
	rm -f stamp-stop
	@date >$@

.endfor

stamp-stop:
	sleep 2  # XXX
	-${SUDO} pkill -f '^${DUMPCMD}'
	rm -f stamp-bpf*
	@date >$@

# Set variables so that make runs with and without obj directory.
# Only do that if necessary to keep visible output short.
.if ${.CURDIR} == ${.OBJDIR}
PYTHON =	python3 -u ./
.else
PYTHON =	env PYTHONPATH=${.OBJDIR} python3 -u ${.CURDIR}/
.endif

# ping

REGRESS_TARGETS +=	run-ping
run-ping: stamp-bpf
	# Ping localhost on loopback
	/sbin/ping -n -w 1 -c 1 -V ${N1} 127.0.0.${N1}
	/sbin/ping -n -w 1 -c 1 -V ${N2} 127.0.0.${N2}

REGRESS_TARGETS +=	run-ping6
run-ping6: stamp-bpf
	# Ping localhost on loopback
	/sbin/ping6 -n -w 1 -c 1 -V ${N1} fe80::${N1}%lo${N1}
	/sbin/ping6 -n -w 1 -c 1 -V ${N2} fe80::${N2}%lo${N2}

REGRESS_TARGETS +=	run-bpf-ping
run-bpf-ping: stamp-stop
	# Check that ping packet went through loopback.
	grep ' 127.0.0.${N1}: icmp: echo request' lo${N1}.tcpdump
	grep ' 127.0.0.${N2}: icmp: echo request' lo${N2}.tcpdump
	grep ' fe80:.*::${N1}: icmp6: echo request' lo${N1}.tcpdump
	grep ' fe80:.*::${N2}: icmp6: echo request' lo${N2}.tcpdump
	! grep ': icmp: echo request' pflog0.tcpdump
	! grep ': icmp6: echo request' pflog0.tcpdump

# ping with RR option

REGRESS_TARGETS +=	run-ping-record
run-ping-record: stamp-bpf
	# Ping localhost with record route option
	/sbin/ping -n -w 1 -c 1 -V ${N1} -R 127.0.0.${N1}
	! /sbin/ping -n -w 1 -c 1 -V ${N2} -R 127.0.0.${N2}

REGRESS_TARGETS +=	run-bpf-ping-record
run-bpf-ping-record: stamp-stop
	# Check that ping packet with options is in pflog0.
	grep ' 127.0.0.${N1}: icmp: echo request .*\
	    optlen=40 RR' lo${N1}.tcpdump
	grep ' 127.0.0.${N2}: icmp: echo request .*\
	    optlen=40 RR' pflog0.tcpdump

# icmp

REGRESS_TARGETS +=	run-icmp
run-icmp: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp.py N2

REGRESS_TARGETS +=	run-icmp6
run-icmp6: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp6.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp6.py N2

REGRESS_TARGETS +=	run-bpf-icmp
run-bpf-icmp: stamp-stop
	# Check that icmp packet went through loopback.
	grep ' 127.0.0.${N1}: icmp: type-#6' lo${N1}.tcpdump
	grep ' 127.0.0.${N2}: icmp: type-#6' lo${N2}.tcpdump
	grep ' fe80::${N1}: icmp6: type-#6' lo${N1}.tcpdump
	grep ' fe80::${N2}: icmp6: type-#6' lo${N2}.tcpdump
	! grep ': icmp: type-#6' pflog0.tcpdump
	! grep ': icmp6: type-#6' pflog0.tcpdump

# option extension header

REGRESS_TARGETS +=	run-icmp6-hop
run-icmp6-hop: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp6_hop.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp6_hop.py N2

REGRESS_TARGETS +=	run-icmp6-dst
run-icmp6-dst: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp6_dst.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp6_dst.py N2

REGRESS_TARGETS +=	run-bpf-ext
run-bpf-ext: stamp-stop
	# Check that icmp6 packet with extension headers were blocked
	fgrep ' fe80::${N2}: HBH icmp6:' pflog0.tcpdump
	fgrep ' fe80::${N2}: DSTOPT icmp6:' pflog0.tcpdump
	! grep fe80::${N1} pflog0.tcpdump

# icmp with options

REGRESS_TARGETS +=	run-icmp-pad
run-icmp-pad: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp_pad.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp_pad.py N2

REGRESS_TARGETS +=	run-icmp-eol
run-icmp-eol: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp_eol.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp_eol.py N2

REGRESS_TARGETS +=	run-icmp6-pad
run-icmp6-pad: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp6_hop_pad.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp6_hop_pad.py N2

REGRESS_TARGETS +=	run-icmp-max
run-icmp-max: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp_max.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp_max.py N2

REGRESS_TARGETS +=	run-icmp6-max
run-icmp6-max: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp6_hop_max.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp6_hop_max.py N2

REGRESS_TARGETS +=	run-icmp-ra
run-icmp-ra: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp_ra.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp_ra.py N2

REGRESS_TARGETS +=	run-icmp6-ra
run-icmp6-ra: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp6_hop_ra.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp6_hop_ra.py N2

REGRESS_TARGETS +=	run-icmp-bad
run-icmp-bad: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp_bad.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp_bad.py N2

REGRESS_TARGETS +=	run-icmp6-bad
run-icmp6-bad: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp6_hop_bad.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp6_hop_bad.py N2

REGRESS_TARGETS +=	run-bpf-opts
run-bpf-opts: stamp-stop
	# Check that icmp packet with options were blocked
	grep ' 127.0.0.${N2}:.* optlen=4 NOP NOP NOP NOP)' pflog0.tcpdump
	grep ' 127.0.0.${N2}:.* optlen=4 NOP EOL-2)' pflog0.tcpdump
	grep ' 127.0.0.${N2}:.* optlen=40 NOP ' pflog0.tcpdump
	grep ' 127.0.0.${N2}:.* optlen=8 NOP IPOPT-148{4} NOP ' pflog0.tcpdump
	grep ' 127.0.0.${N2}:.* optlen=4 IPOPT-3{4})' pflog0.tcpdump
	grep ' fe80::${N2}: HBH icmp6:.* (len 28,' pflog0.tcpdump
	grep ' fe80::${N2}: HBH icmp6:.* (len 284,' pflog0.tcpdump
	grep ' fe80::${N2}: HBH (rtalert: 0x0000) icmp6:' pflog0.tcpdump
	grep ' fe80::${N2}: HBH (type 0x03: len=0) icmp6:' pflog0.tcpdump
	! grep '127.0.0.${N1}' pflog0.tcpdump
	! grep 'fe80::${N1}' pflog0.tcpdump

# multicast with router alert

REGRESS_TARGETS +=	run-igmp
run-igmp: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}igmp_ra.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}igmp_ra.py N2

REGRESS_TARGETS +=	run-icmp6-mld
run-icmp6-mld: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp6_mld_ra.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp6_mld_ra.py N2

REGRESS_TARGETS +=	run-bpf-mcast
run-bpf-mcast: stamp-stop
	# Check that multicast protocol packet with router alert passed
	grep '127.0.0.${N2} > 224.0.0.1:\
	    igmp query .* IPOPT-148{4}' lo${N2}.tcpdump
	grep 'fe80::${N2} > ff02::1:\
	    HBH (rtalert:.* icmp6: multicast ' lo${N2}.tcpdump
	! grep '127.0.0.${N1}' pflog0.tcpdump
	! grep 'fe80::${N1}' pflog0.tcpdump
	! grep '127.0.0.${N2}' pflog0.tcpdump
	! grep 'fe80::${N2}' pflog0.tcpdump
	! grep '224.0.0.1' pflog0.tcpdump
	! grep 'ff02::1' pflog0.tcpdump

REGRESS_TARGETS +=	run-igmp-bad
run-igmp-bad: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}igmp_bad.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}igmp_bad.py N2

REGRESS_TARGETS +=	run-icmp6-mld-bad
run-icmp6-mld-bad: stamp-bpf
	${SUDO} /sbin/route -T ${N1} exec ${PYTHON}icmp6_mld_bad.py N1
	${SUDO} /sbin/route -T ${N2} exec ${PYTHON}icmp6_mld_bad.py N2

REGRESS_TARGETS +=	run-bpf-mcast-bad
run-bpf-mcast-bad: stamp-stop
	# Check that multicast protocol packet with options were blocked
	grep '127.0.0.${N2} > 224.0.0.1:\
	    igmp query .* IPOPT-3{4}' pflog0.tcpdump
	grep 'fe80::${N2} > ff02::1:\
	    HBH (type 0x03:.* icmp6: multicast ' pflog0.tcpdump
	! grep '127.0.0.${N1}' pflog0.tcpdump
	! grep 'fe80::${N1}' pflog0.tcpdump

CLEANFILES +=	addr.py *.pyc *.tcpdump *.log stamp-*

.include <bsd.regress.mk>
