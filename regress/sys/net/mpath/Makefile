#	$OpenBSD: Makefile,v 1.2 2024/04/19 22:20:36 bluhm Exp $

# Copyright (c) 2024 Alexander Bluhm <bluhm@openbsd.org>
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

# Regression test for multipath routing.

# Test that routes set with the -mpath flag distribute the traffic.
# Each route has a different gateway and the UDP packets have different
# source addresses.  The destination address is identical for all
# routes and packets.  Analyze the use counter in netstat -r output
# to check that multiple routes have been used.  Currently this works
# only for IPv4.  sysctl net.inet.ip.multipath and net.inet6.ip6.multipath
# are enabled before testing and reset afterwards.  All routes and
# interface addresses are created on loopback in a separate routing
# domain.

# Set up loopback interface in a different routing domain.
# Create multipath routes on this loopback.
# Send a bunch of packets with multiple source IP to same destination.
# Count in netstart -r that most routes have been used.

# This test uses routing domain and interface number 11.
# Adjust it here, if you want to use something else.
N1 =		11
NUMS =		${N1}
IPS !=		jot 100 100

# Traffic distribution has not been implemented for IPv6.
REGRESS_EXPECTED_FAILURES +=	run-netstat6

.include <bsd.own.mk>

.PHONY: busy-rdomains ifconfig unconfig pfctl

REGRESS_SETUP_ONCE +=	busy-rdomains
busy-rdomains:
	# Check if rdomains are busy.
.for n in ${NUMS}
	@if /sbin/ifconfig | grep -v '^lo$n:' | grep ' rdomain $n '; then\
	    echo routing domain $n is already used >&2; exit 1; fi
.endfor

REGRESS_SETUP_ONCE +=	multipath
multipath:
	${SUDO} /sbin/sysctl net.inet.ip.multipath=1
	${SUDO} /sbin/sysctl net.inet6.ip6.multipath=1

REGRESS_CLEANUP +=	singlepath
singlepath:
	${SUDO} /sbin/sysctl net.inet.ip.multipath=0
	${SUDO} /sbin/sysctl net.inet6.ip6.multipath=0

REGRESS_SETUP_ONCE +=	ifconfig
ifconfig: unconfig
	# Create and configure loopback interfaces.
.for n in ${NUMS}
	${SUDO} /sbin/ifconfig lo$n rdomain $n
	${SUDO} /sbin/ifconfig lo$n inet 10.0.0.1/8
	${SUDO} /sbin/ifconfig lo$n inet 10.0.0.$n alias
.for i in ${IPS}
	${SUDO} /sbin/ifconfig lo$n inet 10.0.$n.$i alias
	${SUDO} /sbin/route -n -T $n add -inet -blackhole -mpath -host \
	    10.$n.0.0 10.0.$n.$i
.endfor
	${SUDO} /sbin/ifconfig lo$n inet6 fc00::$n alias
.for i in ${IPS}
	${SUDO} /sbin/ifconfig lo$n inet6 fc00::$n:$i alias
	${SUDO} /sbin/route -n -T $n add -inet6 -blackhole -mpath -host \
	    fc00::$n:0:0 fc00::$n:$i
.endfor
	# Wait until IPv6 addresses are no longer tentative.
	for i in `jot 50`; do\
	    if ! /sbin/ifconfig lo$n | fgrep -q tentative; then\
		    break;\
	    fi;\
	    sleep .1;\
	done
	! /sbin/ifconfig lo${N1} | fgrep tentative
.endfor

REGRESS_CLEANUP +=	unconfig
unconfig:
	# Destroy interfaces.
.for n in ${NUMS}
	-${SUDO} /sbin/ifconfig lo$n rdomain $n
.for i in ${IPS}
	-${SUDO} /sbin/route -n -T $n delete -inet6 -host \
	    fc00::$n:0:0 fc00::$n:$i
	-${SUDO} /sbin/route -n -T $n delete -inet -host \
	    10.$n.0.0 10.0.$n.$i
.endfor
	-${SUDO} /sbin/ifconfig lo$n -inet -inet6
	-${SUDO} /sbin/ifconfig lo$n destroy
.endfor
	rm -f stamp-ifconfig

REGRESS_TARGETS +=	run-netcat
run-netcat netstat.log:
	# count UDP IPv6 packets used with multipath routes
	rm -f netstat.log
.for i in ${IPS}
	/usr/bin/nc -4 -V${N1} -u -z -s10.0.${N1}.$i 10.${N1}.0.0 discard
.endfor
	/usr/bin/netstat -T${N1} -f inet -rn >netstat.log

REGRESS_TARGETS +=	run-netstat
run-netstat: netstat.log
	# check route distribution of IPv4 packets
	awk 'BEGIN{ mpath = uses = used = maxuse = 0; }\
	    /^10.${N1}.0.0 /{\
		if ($$3 == "UGHSBP") mpath++;\
		if ($$5 > 0) used++;\
		if ($$5 > max) max = $$5;\
		uses += $$5;\
	    }\
	    END{\
		print "mpath "mpath", uses "uses", max "max", used "used;\
		if (mpath != 100) { print "not 100 mpath: " mpath; exit 1; }\
		if (uses != 100) { print "not 100 uses: " uses; exit 1; }\
		if (max > 30) { print "max more than 30: " max; exit 1; }\
		if (used < 50) { print "used less than 50: " used; exit 1; }\
	    }' \
	    netstat.log

REGRESS_TARGETS +=	run-netcat6
run-netcat6 netstat6.log:
	# count UDP IPv6 packets used with multipath routes
	rm -f netstat6.log
.for i in ${IPS}
	/usr/bin/nc -6 -V${N1} -u -z -sfc00::${N1}:$i fc00::${N1}:0:0 discard
.endfor
	/usr/bin/netstat -T${N1} -f inet6 -rn >netstat6.log

REGRESS_TARGETS +=	run-netstat6
run-netstat6: netstat6.log
	# check route distribution of IPv6 packets
	awk 'BEGIN{ mpath = used = uses = max = 0; }\
	    /^fc00::${N1}:0:0 /{\
		if ($$3 == "UGHSBP") mpath++;\
		if ($$5 > 0) used++;\
		if ($$5 > max) max = $$5;\
		uses += $$5;\
	    }\
	    END{\
		print "mpath "mpath", uses "uses", max "max", used "used;\
		if (mpath != 100) { print "not 100 mpath: " mpath; exit 1; }\
		if (uses != 100) { print "not 100 uses: " uses; exit 1; }\
		if (max > 30) { print "max more than 30: " max; exit 1; }\
		if (used < 50) { print "used less than 50: " used; exit 1; }\
	    }' \
	    netstat6.log

CLEANFILES +=	*.log stamp-*

.include <bsd.regress.mk>
