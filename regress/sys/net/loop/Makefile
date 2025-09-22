#	$OpenBSD: Makefile,v 1.11 2023/10/19 18:36:40 anton Exp $

# Copyright (c) 2017-2020 Alexander Bluhm <bluhm@openbsd.org>
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
# Try to ping existing and non existing addresses in these domains.
# Also test pinging to different rdomains via pf.  Check that the
# ttl is decremented while looping though loopback interfaces.

# This test uses routing domain and interface number 11 and 12.
# Adjust it here, if you want to use something else.
N1 =		11
N2 =		12
NUMS =		${N1} ${N2}

.include <bsd.own.mk>

.if ! (make(clean) || make(cleandir) || make(obj))

SYSCTL_FORWARDING !=	sysctl net.inet.ip.forwarding
.if ${SYSCTL_FORWARDING:C/.*=//} != 1
regress:
	@echo sysctl: "${SYSCTL_FORWARDING}"
	@echo Set sysctl to 1 to run this regress.
	@echo SKIPPED
.endif

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
	${SUDO} /sbin/route -n -T $n add -inet -host 10.6.6.6 127.0.0.1
	${SUDO} /sbin/route -n -T $n add -inet -host 10.7.7.7 127.0.0.1
.endfor
	${SUDO} /sbin/route -n -T ${N1} add -inet -host 127.0.0.${N2} 127.0.0.1
	${SUDO} /sbin/route -n -T ${N2} add -inet -host 127.0.0.${N1} 127.0.0.1
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
	-${SUDO} /sbin/ifconfig lo$n destroy
.endfor
	rm -f stamp-ifconfig

addr.py: Makefile
	# Create python include file containing the addresses.
	rm -f $@ $@.tmp
.for var in N1 N2
	echo '${var}="${${var}}"' >>$@.tmp
	echo 'IF_${var}="lo${${var}}"' >>$@.tmp
	echo 'ADDR_${var}="127.0.0.${${var}}"' >>$@.tmp
.endfor
	mv $@.tmp $@

REGRESS_SETUP_ONCE +=	pfctl
pfctl: addr.py pf.conf
	# Load the pf rules into the kernel.
	cat addr.py ${.CURDIR}/pf.conf | /sbin/pfctl -n -f -
	cat addr.py ${.CURDIR}/pf.conf | ${SUDO} /sbin/pfctl -a regress -f -

# run tcpdump on lo devices
DUMPCMD =	/usr/sbin/tcpdump -l -e -vvv -s 2048 -ni

stamp-bpf: stamp-bpf-${N1} stamp-bpf-${N2}
	sleep 2  # XXX
	@date >$@

.for n in ${N1} ${N2}

stamp-bpf-$n: stamp-ifconfig
	rm -f lo$n.tcpdump
	${SUDO} pkill -f '^${DUMPCMD} lo$n' || true
	${SUDO} ${DUMPCMD} lo$n >lo$n.tcpdump &
	rm -f stamp-stop
	@date >$@

.endfor

stamp-stop:
	sleep 2  # XXX
	-${SUDO} pkill -f '^${DUMPCMD}'
	rm -f stamp-bpf*
	@date >$@

.for n in ${N1} ${N2}

REGRESS_TARGETS +=	run-ping-local-$n
run-ping-local-$n: stamp-bpf
	# Ping localhost in routing domain $n.
	/sbin/ping -n -w 1 -c 1 -V $n 127.0.0.1

REGRESS_TARGETS +=	run-ping-loop-$n
run-ping-loop-$n: stamp-bpf
	# Ping non existing address with loopback route in routing domain $n.
	! /sbin/ping -n -w 1 -c 1 -V $n 10.6.6.6

REGRESS_TARGETS +=	run-ping-address-$n
run-ping-address-$n:
	# Ping local address in routing domain $n.
	/sbin/ping -n -w 1 -c 1 -V $n 127.0.0.$n

.endfor

REGRESS_TARGETS +=	run-ping-rdomain-pass
run-ping-rdomain-pass:
	# Pass ping packets between routing domains with pf rule.
	/sbin/ping -n -w 1 -c 1 -V ${N1} 127.0.0.${N2}

REGRESS_TARGETS +=	run-ping-rdomain-block
run-ping-rdomain-block:
	# Check that reverse direction without pf rule is not allowed.
	! /sbin/ping -n -w 1 -c 1 -V ${N2} 127.0.0.${N1}

REGRESS_TARGETS +=	run-ping-rdomain-loop
run-ping-rdomain-loop: stamp-bpf
	# Ping non existing address and loop between routing domains.
	! /sbin/ping -n -w 1 -c 1 -V ${N1} 10.7.7.7

.for n in ${N1} ${N2}

REGRESS_TARGETS +=	run-bpf-local-$n
run-bpf-local-$n: stamp-stop
	# Check that the ping packet went through loopback.
	grep '127.0.0.1 > 127.0.0.1: icmp: echo request' lo$n.tcpdump

REGRESS_TARGETS +=	run-bpf-loop-$n
run-bpf-loop-$n: stamp-stop
	# Check that the ping packet went multiple times through loopback.
	grep '[0-9] 127.0.0.1 > 10.6.6.6: icmp: echo request .*ttl 255,' \
	    lo$n.tcpdump
	grep '[0-9] 127.0.0.1 > 10.6.6.6: icmp: echo request .* \[ttl 1\]' \
	    lo$n.tcpdump

.endfor

REGRESS_TARGETS +=	run-bpf-rdomain-loop-${N1}
run-bpf-rdomain-loop-${N1}: stamp-stop
	# Check the ping packet went multiple times in routing domains.
	grep '[0-9] 127.0.0.1 > 10.7.7.7: icmp: echo request .*ttl 255,' \
	    lo${N1}.tcpdump
	! grep '[0-9] 127.0.0.1 > 10.7.7.7: icmp: echo request .*ttl 254,' \
	    lo${N1}.tcpdump
	grep '[0-9] 127.0.0.1 > 10.7.7.7: icmp: echo request .* \[ttl 1\]' \
	    lo${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-rdomain-loop-${N2}
run-bpf-rdomain-loop-${N2}: stamp-stop
	# Check the ping packet went multiple times in routing domains.
	grep '[0-9] 127.0.0.1 > 10.7.7.7: icmp: echo request .*ttl 254,' \
	    lo${N2}.tcpdump
	grep '[0-9] 127.0.0.1 > 10.7.7.7: icmp: echo request .*ttl 2,' \
	    lo${N2}.tcpdump
	! grep '[0-9] 127.0.0.1 > 10.7.7.7: icmp: echo request .* \[ttl 1\]' \
	    lo${N2}.tcpdump

CLEANFILES +=	addr.py *.pyc *.tcpdump *.log stamp-*

.include <bsd.regress.mk>
