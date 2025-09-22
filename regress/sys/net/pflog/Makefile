#	$OpenBSD: Makefile,v 1.10 2023/10/19 18:36:40 anton Exp $

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

# Basic testing of the pflog(4) interface.  Create special routing
# domain, load rules into pf(4) regress anchor, tcpdump on pflog,
# send packets over lo(4), grep for expected result in tcpdump output.

# This test uses routing domain 11 and pflog interface number 11, 12, 13.
# Adjust it here, if you want to use something else.
N1 =		11
N2 =		12
N3 =		13
N =		${N1}
NUMS =		${N1} ${N2} ${N3}
IPS =		1 2 3 4 5 6 11 12 14

UID !!=		id -u

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
		grep -w -e lo${N1} -e lo${N2} -e lo${N3} || :
.if ! empty(PF_SKIP)
regress:
	@echo pf skip: "${PF_SKIP}"
	@echo Do not set skip on interface lo, lo${N1}, lo${N2}, or lo${N3}.
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

SYSCTL_FORWARDING !=	sysctl net.inet.ip.forwarding
SYSCTL_FORWARDING6 !=	sysctl net.inet6.ip6.forwarding
.if ${SYSCTL_FORWARDING:C/.*=//} != 1 || ${SYSCTL_FORWARDING6:C/.*=//} != 1
# Do not skip, but run tests.  Although they fail, their packets are logged.
REGRESS_EXPECTED_FAILURES =	run-ping-14 run-ping6-14
.endif

.endif

.PHONY: busy-rdomains ifconfig unconfig pfctl

REGRESS_SETUP_ONCE +=	busy-rdomains
busy-rdomains:
	# Check if rdomains are busy.
	@if /sbin/ifconfig | grep -v '^lo$N:' | grep ' rdomain $N '; then\
	    echo routing domain $N is already used >&2; exit 1; fi

REGRESS_SETUP_ONCE +=	ifconfig
ifconfig: unconfig
	# Create and configure pflog and loopback interfaces.
.for n in ${NUMS}
	${SUDO} /sbin/ifconfig pflog$n create
.endfor
	${SUDO} /sbin/ifconfig lo$N rdomain $N
	${SUDO} /sbin/ifconfig lo$N inet 127.0.0.1/8
	${SUDO} /sbin/ifconfig lo$N inet6 ::1/128
.for i in ${IPS} 21 22 23 24
	${SUDO} /sbin/ifconfig lo$N inet 169.254.0.$i/32 alias
	${SUDO} /sbin/ifconfig lo$N inet6 fc00::$i/128
.endfor
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
unconfig: stamp-stop
	# Destroy interfaces.
	-${SUDO} /sbin/ifconfig lo$N rdomain $N
.for i in ${IPS} 21 22 23 24
	-${SUDO} /sbin/ifconfig lo$N inet 169.254.0.$i delete
	-${SUDO} /sbin/ifconfig lo$N inet6 fc00::$i delete
.endfor
	-${SUDO} /sbin/ifconfig lo$N inet 127.0.0.1 delete
	-${SUDO} /sbin/ifconfig lo$N inet6 ::1 delete
.for n in ${NUMS}
	-${SUDO} /sbin/ifconfig pflog$n destroy
.endfor
	-${SUDO} /sbin/ifconfig lo$N destroy
	rm -f stamp-ifconfig

addr.py: Makefile
	# Create python include file containing the addresses.
	rm -f $@ $@.tmp
	echo 'N="$N"' >>$@.tmp
	echo 'LO="lo$N"' >>$@.tmp
.for var in N1 N2 N3
	echo '${var}="${${var}}"' >>$@.tmp
	echo 'PFLOG_${var}="pflog${${var}}"' >>$@.tmp
.endfor
	mv $@.tmp $@

REGRESS_SETUP_ONCE +=	pfctl
pfctl: addr.py pf.conf
	# Load the pf rules into the kernel.
	cat addr.py ${.CURDIR}/pf.conf | /sbin/pfctl -n -f -
	cat addr.py ${.CURDIR}/pf.conf | ${SUDO} /sbin/pfctl -a regress -f -

# Run tcpdump on pflog devices.
DUMPCMD =	/usr/sbin/tcpdump -l -e -vvv -s 2048 -ni

stamp-bpf: stamp-bpf-${N1} stamp-bpf-${N2} stamp-bpf-${N3}
	sleep 2  # XXX
	@date >$@

.for n in ${NUMS}

stamp-bpf-$n: stamp-ifconfig
	rm -f pflog$n.tcpdump
	${SUDO} pkill -f '^${DUMPCMD} pflog$n' || true
	${SUDO} ${DUMPCMD} pflog$n >pflog$n.tcpdump &
	rm -f stamp-stop
	@date >$@

.endfor

stamp-stop:
	sleep 2  # XXX
	-${SUDO} pkill -f '^${DUMPCMD}'
	rm -f stamp-bpf*
	@date >$@

.for i in ${IPS}
REGRESS_TARGETS +=	run-ping-$i
run-ping-$i: stamp-bpf
	/sbin/ping -n -w 1 -c 1 -V $N 169.254.0.$i

REGRESS_TARGETS +=	run-ping6-$i
run-ping6-$i: stamp-bpf
	/sbin/ping6 -n -w 1 -c 1 -V $N fc00::$i

REGRESS_TARGETS +=	run-udp-$i
run-udp-$i: stamp-bpf
	# ignore errors, just send packet fast
	echo foo | nc -u -w 1 -V $N 169.254.0.$i discard &

REGRESS_TARGETS +=	run-udp6-$i
run-udp6-$i: stamp-bpf
	# ignore errors, just send packet fast
	echo foo | nc -u -w 1 -V $N fc00::$i discard &
.endfor

REGRESS_TARGETS +=	run-ping6-0
run-ping6-0: stamp-bpf
	/sbin/ping6 -n -w 1 -c 1 -V $N ::1

REGRESS_TARGETS +=	run-udp6-0
run-udp6-0: stamp-bpf
	echo foo | nc -u -w 1 -V $N ::1 discard

.for n in ${NUMS}
REGRESS_TARGETS +=	run-bpf-$n
run-bpf-$n: stamp-stop
	# show full logs
	cat pflog$n.tcpdump
.endfor

REGRESS_TARGETS +=	run-bpf-nothing
run-bpf-nothing: stamp-stop
	# rule with pflog${N3} is never used
	! grep . pflog${N3}.tcpdump

REGRESS_TARGETS +=	run-bpf-everything
run-bpf-everything: stamp-stop
	# rule with pflog${N2} matches on every packet
.for i in ${IPS}
	grep 'regress\.1/.* > 169.254.0.$i:' pflog${N2}.tcpdump
.endfor

REGRESS_TARGETS +=	run-bpf-everything6
run-bpf-everything6: stamp-stop
	# rule with pflog${N2} matches on every packet
.for i in ${IPS}
	grep 'regress\.1/.* > fc00::$i:' pflog${N2}.tcpdump
.endfor

REGRESS_TARGETS +=	run-bpf-all
run-bpf-all: stamp-stop
	# reply without keep state
	grep 'regress\.3/.* 169.254.0.1 > 169.254.0.1:\
	    icmp: echo request' pflog${N1}.tcpdump
	grep 'regress\.3/.* 169.254.0.1 > 169.254.0.1:\
	    icmp: echo reply' pflog${N1}.tcpdump
	# no reply with keep state and without all
	grep 'regress\.4/.* 169.254.0.2 > 169.254.0.2:\
	    icmp: echo request' pflog${N1}.tcpdump
	! grep 'regress\.4/.* 169.254.0.2 >169.254.0.2:\
	    icmp: echo reply' pflog${N1}.tcpdump
	# reply with keep state and with all
	grep 'regress\.5/.* 169.254.0.3 > 169.254.0.3:\
	    icmp: echo request' pflog${N1}.tcpdump
	# XXX anchor name missing
	grep '/.* 169.254.0.3 > 169.254.0.3:\
	    icmp: echo reply' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-all6
run-bpf-all6: stamp-stop
	# reply without keep state
	grep 'regress\.11/.* fc00::1 > fc00::1:\
	    icmp6: echo request' pflog${N1}.tcpdump
	grep 'regress\.11/.* fc00::1 > fc00::1:\
	    icmp6: echo reply' pflog${N1}.tcpdump
	# no reply with keep state and without all
	grep 'regress\.12/.* fc00::2 > fc00::2:\
	    icmp6: echo request' pflog${N1}.tcpdump
	! grep 'regress\.12/.* fc00::2 > fc00::2:\
	    icmp6: echo reply' pflog${N1}.tcpdump
	# reply with keep state and with all
	grep 'regress\.13/.* fc00::3 > fc00::3:\
	    icmp6: echo request' pflog${N1}.tcpdump
	# XXX anchor name missing
	grep '/.* fc00::3 > fc00::3:\
	    icmp6: echo reply' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-user
run-bpf-user: stamp-stop
	# out rule creates log entry with uid
	grep 'regress\.6/.* pass out on lo$N: \[uid ${UID}, pid [0-9]*\]\
	    169.254.0.4\.[0-9]* > 169.254.0.4\.9:\
	    .* udp [0-9]' pflog${N1}.tcpdump
	# in rule has no uid at log entry
	grep 'regress\.6/.* pass in on lo$N:\
	    169.254.0.4\.[0-9]* > 169.254.0.4\.9:\
	    .* udp [0-9]' pflog${N1}.tcpdump
	# icmp has no uid at log entry
	grep 'regress\.6/.* pass out on lo$N:\
	    169.254.0.4 > 169.254.0\.4:\
	    icmp: echo request' pflog${N1}.tcpdump
	# rule without user has no uid in log entry
	grep 'regress\.3/.* pass out on lo$N:\
	    169.254.0.1\.[0-9]* > 169.254.0.1\.9:\
	    .* udp [0-9]' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-user6
run-bpf-user6: stamp-stop
	# out rule creates log entry with uid
	grep 'regress\.14/.* pass out on lo$N: \[uid ${UID}, pid [0-9]*\]\
	    fc00::4\.[0-9]* > fc00::4\.9:.* udp [0-9]' pflog${N1}.tcpdump
	# in rule has no uid at log entry
	grep 'regress\.14/.* pass in on lo$N:\
	    fc00::4\.[0-9]* > fc00::4\.9:.* udp [0-9]' pflog${N1}.tcpdump
	# icmp has no uid at log entry
	grep 'regress\.14/.* pass out on lo$N:\
	    fc00::4 > fc00::4: icmp6: echo request' pflog${N1}.tcpdump
	# rule without user has no uid in log entry
	grep 'regress\.11/.* pass out on lo$N:\
	    fc00::1\.[0-9]* > fc00::1\.9:.* udp [0-9]' pflog${N1}.tcpdump

run-bpf-matches run-bpf-matches6:
	# XXX The log matches keyword seems to be totally broken.
	# pf_log_matches() is never called.  Investigate later.
	@echo DISABLED

REGRESS_TARGETS +=	run-bpf-matches
run-bpf-matches: stamp-stop
	grep 'regress\.9/.* .*: 169.254.0.6 > 169.254.0.6:\
	    icmp: echo request' pflog${N1}.tcpdump
	! grep 'regress\.8/.* icmp: echo request' pflog${N1}.tcpdump
	! grep 'regress\.7/.* icmp: echo request' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-rdr
run-bpf-rdr: stamp-stop
	# loopback input logs redirected packet
	grep 'regress\.2/.* pass in .*:.* 169.254.0.11 > 169.254.0.21:\
	    icmp: echo request' pflog${N1}.tcpdump
	# loopback output redirects and logs original packet
	grep 'regress\.18/.* pass out .*:.* 169.254.0.11 > 169.254.0.11:\
	    icmp: echo request' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-rdr6
run-bpf-rdr6: stamp-stop
	# loopback input logs redirected packet
	grep 'regress\.10/.* pass in .*:.* fc00::11 > fc00::21:\
	    icmp6: echo request' pflog${N1}.tcpdump
	# loopback output redirects and logs original packet
	grep 'regress\.20/.* pass out .*:.* fc00::11 > fc00::11:\
	    icmp6: echo request' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-nat
run-bpf-nat: stamp-stop
	# loopback input logs redirected packet
	grep 'regress\.2/.* pass in .*:.* 169.254.0.22 > 169.254.0.12:\
	    icmp: echo request' pflog${N1}.tcpdump
	# loopback output redirects and logs original packet
	grep 'regress\.19/.* pass out .*:.* 169.254.0.12 > 169.254.0.12:\
	    icmp: echo request' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-nat6
run-bpf-nat6: stamp-stop
	# loopback input logs redirected packet
	grep 'regress\.10/.* pass in .*:.* fc00::22 > fc00::12:\
	    icmp6: echo request' pflog${N1}.tcpdump
	# loopback output redirects and logs original packet
	grep 'regress\.21/.* pass out .*:.* fc00::12 > fc00::12:\
	    icmp6: echo request' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-af
run-bpf-af: stamp-stop
	# pf in rule logs original IPv4 packet
	grep 'regress\.22/.* pass in .*:.* 169.254.0.14 > 169.254.0.14:\
	     icmp: echo request' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-af6
run-bpf-af6: stamp-stop
	# pf in rule logs original IPv6 packet
	grep 'regress\.23/.* pass in .*:.* fc00::14 > fc00::14:\
	     icmp6: echo request' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-rewrite
run-bpf-rewrite: stamp-stop
	# rdr-to address has been rewritten
	grep '\[rewritten: src 169.254.0.11:[0-9]*, dst 169.254.0.21:[0-9]*\]\
	    169.254.0.11 > 169.254.0.11' pflog${N1}.tcpdump
	# nat-to address has been rewritten
	grep '\[rewritten: src 169.254.0.22:[0-9]*, dst 169.254.0.12:[0-9]*\]\
	    169.254.0.12 > 169.254.0.12' pflog${N1}.tcpdump
	# af-to address has been rewritten
	grep '\[rewritten: src fc00::23:[0-9]*, dst fc00::24:[0-9]*\]\
	    169.254.0.14 > 169.254.0.14' pflog${N1}.tcpdump

REGRESS_TARGETS +=	run-bpf-rewrite6
run-bpf-rewrite6: stamp-stop
	# rdr-to address has been rewritten
	grep '\[rewritten: src fc00::11:[0-9]*, dst fc00::21:[0-9]*\]\
	    fc00::11 > fc00::11' pflog${N1}.tcpdump
	# nat-to address has been rewritten
	grep '\[rewritten: src fc00::22:[0-9]*, dst fc00::12:[0-9]*\]\
	    fc00::12 > fc00::12' pflog${N1}.tcpdump
	# af-to address has been rewritten
	grep '\[rewritten: src 169.254.0.23:[0-9]*, dst 169.254.0.24:[0-9]*\]\
	    fc00::14 > fc00::14' pflog${N1}.tcpdump

CLEANFILES +=	addr.py *.pyc *.tcpdump *.log stamp-*

.include <bsd.regress.mk>
