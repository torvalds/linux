#	$OpenBSD: Makefile,v 1.5 2023/10/19 18:36:40 anton Exp $

# Copyright (c) 2021 Alexander Bluhm <bluhm@openbsd.org>
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

# Setup two pair(4) and one lo(4) interface in different routing
# domains and with different interface MTU.

# pair1 with MTU 8000 <-> pair2 <-> loopback3

# Both pairs are patched.  Between pair2 and loopback3 pf switches
# the routing table.  Do extensive ping tests to check that packets
# get through.  Use tcpbench for TCP path MTU discovery.  The jumbo
# frames with MTU 8000 together with switching the routing domain
# trigger unusual code paths in the kernel.

# This test uses routing domain 11, 12, 13.
# Adjust it here, if you want to use something else.
N1 =		11
N2 =		12
N3 =		13
NUMS =		${N1} ${N2} ${N3}
PAIRS =		${N1} ${N2}
IP_${N1} =	169.254.1.${N1}
IP6_${N1} =	fc00:0:0:1::${N1}
IP_${N2} =	169.254.1.${N2}
IP6_${N2} =	fc00:0:0:1::${N2}
IP_${N3} =	169.254.0.${N3}
IP6_${N3} =	fc00::${N3}

.include <bsd.own.mk>

.if ! (make(clean) || make(cleandir) || make(obj))

SYSCTL_FORWARDING !=	sysctl net.inet.ip.forwarding
SYSCTL_FORWARDING6 !=	sysctl net.inet6.ip6.forwarding
.if ${SYSCTL_FORWARDING:C/.*=//} != 1 || ${SYSCTL_FORWARDING6:C/.*=//} != 1
regress:
	@echo sysctl: "${SYSCTL_FORWARDING}" "${SYSCTL_FORWARDING6}"
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
		grep -w -e lo${N1} -e lo${N2} -e lo${N3} || :
.if ! empty(PF_SKIP)
regress:
	@echo pf skip: "${PF_SKIP}"
	@echo Do not set skip on interface lo, lo${N1}, lo${N2} or lo${N3}.
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

.PHONY: ifconfig unconfig pfctl

REGRESS_SETUP_ONCE +=	ifconfig
ifconfig: unconfig
	# Create and configure pflog and loopback interfaces.
.for n in ${NUMS}
	${SUDO} /sbin/ifconfig lo$n rdomain $n
	${SUDO} /sbin/ifconfig lo$n inet 127.0.0.1/8
	${SUDO} /sbin/ifconfig lo$n inet6 ::1/128
	${SUDO} /sbin/route -n -T $n add -inet 169.254.0.0/16 127.0.0.1
	${SUDO} /sbin/route -n -T $n add -inet6 fc00::/48 ::1
.endfor
.for n in ${PAIRS}
	${SUDO} /sbin/ifconfig pair$n rdomain $n
	${SUDO} /sbin/ifconfig pair$n inet ${IP_$n}/24
	${SUDO} /sbin/ifconfig pair$n inet6 ${IP6_$n}/64
.endfor
	${SUDO} /sbin/ifconfig pair${N1} patch pair${N2}
	${SUDO} /sbin/ifconfig lo${N3} inet ${IP_${N3}}/24 alias
	${SUDO} /sbin/ifconfig lo${N3} inet6 ${IP6_${N3}}/64
	${SUDO} /sbin/ifconfig pair${N1} mtu 8000
	${SUDO} /sbin/route -n -T ${N1} add -inet ${IP_${N3}} ${IP_${N2}}
	${SUDO} /sbin/route -n -T ${N1} add -inet6 ${IP6_${N3}} ${IP6_${N2}}
	# Wait until IPv6 addresses are no longer tentative.
	for i in `jot 50`; do\
	    if ! { /sbin/ifconfig pair${N1}; /sbin/ifconfig pair${N2};\
		/sbin/ifconfig lo${N3}; } | fgrep -q tentative; then\
		    break;\
	    fi;\
	    sleep .1;\
	done
	! { /sbin/ifconfig pair${N1}; /sbin/ifconfig pair${N2};\
	    /sbin/ifconfig lo${N3}; } | fgrep tentative

REGRESS_CLEANUP +=	unconfig
unconfig:
	# Destroy interfaces.
	-${SUDO} /sbin/route -n -T ${N1} delete -inet ${IP_${N3}}
	-${SUDO} /sbin/route -n -T ${N1} delete -inet6 ${IP6_${N3}}
	-${SUDO} /sbin/ifconfig lo${N3} rdomain ${N3}
	-${SUDO} /sbin/ifconfig lo${N3} inet ${IP_${N3}} delete
	-${SUDO} /sbin/ifconfig lo${N3} inet6 ${IP6_${N3}} delete
.for n in ${PAIRS}
	-${SUDO} /sbin/ifconfig pair$n destroy
.endfor
.for n in ${NUMS}
	-${SUDO} /sbin/route -n -T $n delete -inet 169.254.0.0/16
	-${SUDO} /sbin/route -n -T $n delete -inet6 fc00::/48
	-${SUDO} /sbin/ifconfig lo$n inet 127.0.0.1 delete
	-${SUDO} /sbin/ifconfig lo$n inet6 ::1 delete
	-${SUDO} /sbin/ifconfig lo$n destroy
.endfor
	rm -f stamp-ifconfig

addr.py: Makefile
	# Create python include file containing the addresses.
	rm -f $@ $@.tmp
.for var in N1 N2 N3
	echo '${var}="${${var}}"' >>$@.tmp
	echo 'LO_${var}="lo${${var}}"' >>$@.tmp
	echo 'PAIR_${var}="pair${${var}}"' >>$@.tmp
	echo 'IP_${var}="${IP_${${var}}}"' >>$@.tmp
	echo 'IP6_${var}="${IP6_${${var}}}"' >>$@.tmp
.endfor
	mv $@.tmp $@

REGRESS_SETUP_ONCE +=	pfctl
pfctl: addr.py pf.conf
	# Load the pf rules into the kernel.
	cat addr.py ${.CURDIR}/pf.conf | /sbin/pfctl -n -f -
	cat addr.py ${.CURDIR}/pf.conf | ${SUDO} /sbin/pfctl -a regress -f -

.for f in 1 2 3
.for t in 1 2 3
REGRESS_TARGETS +=	run-ping-$f-$t
run-ping-$f-$t:
	/sbin/ping -c 1 -w 1 -n -V ${N$f} -I ${IP_${N$f}} ${IP_${N$t}}

REGRESS_TARGETS +=	run-ping6-$f-$t
run-ping6-$f-$t:
	/sbin/ping6 -c 1 -w 1 -n -V ${N$f} -I ${IP6_${N$f}} ${IP6_${N$t}}

REGRESS_TARGETS +=	run-tcpbench-$f-$t
run-tcpbench-$f-$t:
	rm -f nc.log
	nc -4 -v -l -V ${N$t} ${IP_${N$t}} 12345 >/dev/null 2>nc.log &
	# Wait until nc is listening.
	for  i in `jot 30`; do\
	    if fgrep -q Listening nc.log; then break; fi; sleep .1; done
	fgrep Listening nc.log
	# Test that path MTU dicovery is working.
	tcpbench -4 -t 5 -V ${N$f} -b ${IP_${N$f}} ${IP_${N$t}}
.if $f == "3" && $t == "1"
	# path MTU discovery must create a dynamic route
	/sbin/route -T ${N$f} -n get -host -inet ${IP_${N$t}} | grep DYNAMIC
.endif

REGRESS_TARGETS +=	run-tcpbench6-$f-$t
run-tcpbench6-$f-$t:
	rm -f nc.log
	nc -6 -v -l -V ${N$t} ${IP6_${N$t}} 12345 >/dev/null 2>nc.log &
	# Wait until nc is listening.
	for  i in `jot 30`; do\
	    if fgrep -q Listening nc.log; then break; fi; sleep .1; done
	fgrep Listening nc.log
	# Test that path MTU dicovery is working.
	tcpbench -6 -t 5 -V ${N$f} -b ${IP6_${N$f}} ${IP6_${N$t}}
.if $f == "3" && $t == "1"
	# path MTU discovery in other rtable does not work in ip6_output()
	/sbin/route -T ${N$f} -n get -host -inet6 ${IP6_${N$t}}
	@echo DISABLED
.endif

.endfor
.endfor

CLEANFILES +=	addr.py *.pyc *.log stamp-*

.include <bsd.regress.mk>
