#	$OpenBSD: Makefile,v 1.9 2023/10/19 18:36:40 anton Exp $

# Copyright (c) 2018-2020 Alexander Bluhm <bluhm@openbsd.org>
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

# Set up two gif interfaces in different routing domains.
# Try to ping existing addresses in these domains with various tunnels.

# This test uses routing domain and interface number 11 and 12.
# Adjust it here, if you want to use something else.
N1 =		11
N2 =		12
NUMS =		${N1} ${N2}

TUNNEL4_SRC_ADDR4_${N1} =	10.188.44.${N1}
TUNNEL4_DST_ADDR4_${N1} =	10.188.44.${N2}
TUNNEL6_SRC_ADDR4_${N1} =	10.188.64.${N1}
TUNNEL6_DST_ADDR4_${N1} =	10.188.64.${N2}
SRC_TUNNEL4_${N1} =		10.188.4.${N1}
DST_TUNNEL4_${N1} =		10.188.4.${N2}
TUNNEL4_SRC_ADDR6_${N1} =	fdd7:e83e:66bc:46::${N1}
TUNNEL4_DST_ADDR6_${N1} =	fdd7:e83e:66bc:46::${N2}
TUNNEL6_SRC_ADDR6_${N1} =	fdd7:e83e:66bc:66::${N1}
TUNNEL6_DST_ADDR6_${N1} =	fdd7:e83e:66bc:66::${N2}
SRC_TUNNEL6_${N1} =		fdd7:e83e:66bc:6::${N1}
DST_TUNNEL6_${N1} =		fdd7:e83e:66bc:6::${N2}

TUNNEL4_SRC_ADDR4_${N2} =	${TUNNEL4_DST_ADDR4_${N1}}
TUNNEL4_DST_ADDR4_${N2} =	${TUNNEL4_SRC_ADDR4_${N1}}
TUNNEL6_SRC_ADDR4_${N2} =	${TUNNEL6_DST_ADDR4_${N1}}
TUNNEL6_DST_ADDR4_${N2} =	${TUNNEL6_SRC_ADDR4_${N1}}
SRC_TUNNEL4_${N2} =		${DST_TUNNEL4_${N1}}
DST_TUNNEL4_${N2} =		${SRC_TUNNEL4_${N1}}
TUNNEL4_SRC_ADDR6_${N2} =	${TUNNEL4_DST_ADDR6_${N1}}
TUNNEL4_DST_ADDR6_${N2} =	${TUNNEL4_SRC_ADDR6_${N1}}
TUNNEL6_SRC_ADDR6_${N2} =	${TUNNEL6_DST_ADDR6_${N1}}
TUNNEL6_DST_ADDR6_${N2} =	${TUNNEL6_SRC_ADDR6_${N1}}
SRC_TUNNEL6_${N2} =		${DST_TUNNEL6_${N1}}
DST_TUNNEL6_${N2} =		${SRC_TUNNEL6_${N1}}

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
	${SUDO} /sbin/ifconfig lo$n inet6 ::1/128
	${SUDO} /sbin/ifconfig lo$n inet ${SRC_TUNNEL4_$n} alias
	${SUDO} /sbin/ifconfig lo$n inet6 ${SRC_TUNNEL6_$n} alias
	${SUDO} /sbin/route -n -T $n add -inet -host ${DST_TUNNEL4_$n} 127.0.0.1
	${SUDO} /sbin/route -n -T $n add -inet6 -host ${DST_TUNNEL6_$n} ::1
	${SUDO} /sbin/ifconfig gif$n4 create rdomain $n tunneldomain $n
	${SUDO} /sbin/ifconfig gif$n4 tunnel ${SRC_TUNNEL4_$n} ${DST_TUNNEL4_$n}
	${SUDO} /sbin/ifconfig gif$n4 inet ${TUNNEL4_SRC_ADDR4_$n}/32\
	    ${TUNNEL4_DST_ADDR4_$n}
	${SUDO} /sbin/ifconfig gif$n4 inet6 ${TUNNEL4_SRC_ADDR6_$n}\
	    ${TUNNEL4_DST_ADDR6_$n}
	${SUDO} /sbin/ifconfig gif$n6 create rdomain $n tunneldomain $n
	${SUDO} /sbin/ifconfig gif$n6 tunnel ${SRC_TUNNEL6_$n}\
	    ${DST_TUNNEL6_$n}
	${SUDO} /sbin/ifconfig gif$n6 inet ${TUNNEL6_SRC_ADDR4_$n}/32\
	    ${TUNNEL6_DST_ADDR4_$n}
	${SUDO} /sbin/ifconfig gif$n6 inet6 ${TUNNEL6_SRC_ADDR6_$n}\
	    ${TUNNEL6_DST_ADDR6_$n}
.endfor
	# Wait until IPv6 addresses are no longer tentative.
	for i in `jot 50`; do\
	    if ! { /sbin/ifconfig lo${N1}; /sbin/ifconfig lo${N2};\
		/sbin/ifconfig gif${N1}4; /sbin/ifconfig gif${N1}6;\
		/sbin/ifconfig gif${N2}4; /sbin/ifconfig gif${N2}6; }\
		| fgrep -q tentative; then\
		    break;\
	    fi;\
	    sleep .1;\
	done
	! { /sbin/ifconfig lo${N1}; /sbin/ifconfig lo${N2};\
	    /sbin/ifconfig gif${N1}4; /sbin/ifconfig gif${N1}6;\
	    /sbin/ifconfig gif${N2}4; /sbin/ifconfig gif${N2}6; }\
	    | fgrep -q tentative

REGRESS_CLEANUP +=	unconfig
unconfig:
.for n in ${NUMS}
	-${SUDO} /sbin/ifconfig lo$n rdomain $n
	-${SUDO} /sbin/ifconfig lo$n inet 127.0.0.1 delete
	-${SUDO} /sbin/ifconfig lo$n inet6 ::1 delete
	-${SUDO} /sbin/ifconfig lo$n inet ${SRC_TUNNEL4_$n} delete
	-${SUDO} /sbin/ifconfig lo$n inet6 ${SRC_TUNNEL6_$n} delete
	-${SUDO} /sbin/ifconfig gif$n4 destroy
	-${SUDO} /sbin/ifconfig gif$n6 destroy
	-${SUDO} /sbin/ifconfig lo$n destroy
.endfor
	rm -f stamp-ifconfig

addr.py: Makefile
	# Create python include file containing the addresses.
	rm -f $@ $@.tmp
.for N in N1 N2
	echo '${N}="${${N}}"' >>$@.tmp
	echo 'IF_${N}="lo${${N}}"' >>$@.tmp
	echo 'DST_TUNNEL4_${N}="${DST_TUNNEL4_${${N}}}"' >>$@.tmp
	echo 'DST_TUNNEL6_${N}="${DST_TUNNEL6_${${N}}}"' >>$@.tmp
.endfor
	mv $@.tmp $@

REGRESS_SETUP_ONCE +=	pfctl
pfctl: addr.py pf.conf
	# Load the pf rules into the kernel.
	cat addr.py ${.CURDIR}/pf.conf | /sbin/pfctl -n -f -
	cat addr.py ${.CURDIR}/pf.conf | ${SUDO} /sbin/pfctl -a regress -f -
	# ifconfig gif inet6 DAD created tunnel states based on old pf rules
	${SUDO} /sbin/pfctl -Fs

.for n in ${N1} ${N2}

REGRESS_TARGETS +=	run-ping-local-$n
run-ping-local-$n:
	# Ping localhost in routing domain $n.
	/sbin/ping -n -w 1 -c 1 -V $n 127.0.0.1

REGRESS_TARGETS +=	run-ping-src-$n
run-ping-src-$n:
	# Ping source address in local routing domain.  SRC_TUNNEL4_$n
	/sbin/ping -n -w 1 -c 1 -V $n ${SRC_TUNNEL4_$n}

REGRESS_TARGETS +=	run-ping-dst-$n
run-ping-dst-$n:
	# Ping destination address in other routing domain.  DST_TUNNEL4_$n
	/sbin/ping -n -w 1 -c 1 -V $n ${DST_TUNNEL4_$n}

REGRESS_TARGETS +=	run-ping-tunnel4-$n
run-ping-tunnel4-$n:
	# Ping IPv4 address through IPv4 tunnel.  TUNNEL4_DST_ADDR4_$n
	/sbin/ping -n -w 1 -c 1 -V $n ${TUNNEL4_DST_ADDR4_$n}

REGRESS_TARGETS +=	run-ping-tunnel6-$n
run-ping-tunnel6-$n:
	# Ping IPv4 address through IPv6 tunnel.  TUNNEL6_DST_ADDR4_$n
	/sbin/ping -n -w 1 -c 1 -V $n ${TUNNEL6_DST_ADDR4_$n}

REGRESS_TARGETS +=	run-ping6-local-$n
run-ping6-local-$n:
	# Ping localhost in routing domain $n.
	/sbin/ping6 -n -w 1 -c 1 -V $n ::1

REGRESS_TARGETS +=	run-ping6-src-$n
run-ping6-src-$n:
	# Ping source address in local routing domain.  SRC_TUNNEL6_$n
	/sbin/ping6 -n -w 1 -c 1 -V $n ${SRC_TUNNEL6_$n}

REGRESS_TARGETS +=	run-ping6-dst-$n
run-ping6-dst-$n:
	# Ping destination address in other routing domain.  DST_TUNNEL6_$n
	/sbin/ping6 -n -w 1 -c 1 -V $n ${DST_TUNNEL6_$n}

REGRESS_TARGETS +=	run-ping6-tunnel4-$n
run-ping6-tunnel4-$n:
	# Ping IPv6 address through IPv4 tunnel.  TUNNEL4_DST_ADDR6_$n
	/sbin/ping6 -n -w 1 -c 1 -V $n ${TUNNEL4_DST_ADDR6_$n}

REGRESS_TARGETS +=	run-ping6-tunnel6-$n
run-ping6-tunnel6-$n:
	# Ping IPv6 address through IPv6 tunnel.  TUNNEL6_DST_ADDR6_$n
	/sbin/ping6 -n -w 1 -c 1 -V $n ${TUNNEL6_DST_ADDR6_$n}

.endfor

CLEANFILES +=	addr.py *.pyc *.log

.include <bsd.regress.mk>
