#	$OpenBSD: Makefile,v 1.6 2025/09/17 22:50:08 bluhm Exp $

# Copyright (c) 2025 Alexander Bluhm <bluhm@openbsd.org>
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

.include <bsd.own.mk>

.if ! (make(clean) || make(cleandir) || make(obj))

.if ! exists(/usr/local/bin/scapy)
regress:
	@echo Install scapy package to run this regress.
	@echo SKIPPED
.endif

.endif

# This test needs a manual setup of two machines
# Set up machines: LOCAL REMOTE
# LOCAL is the machine where this makefile is running.
# REMOTE is running OpenBSD with echo server to test PMTU
# FAKE_NET is a non existing existing network.
# FAKE_NET_ADDR is a non existing machine in a non existing network.
# REMOTE_SSH is the hostname to log in on the REMOTE machine.

# Configure Addresses on the machines.
# Adapt interface and addresse variables to your local setup.
#
LOCAL_IF ?=
REMOTE_SSH ?=

LOCAL_ADDR ?=
REMOTE_ADDR ?=
FAKE_NET ?=
FAKE_NET_ADDR ?=

.if empty (LOCAL_IF) || empty (REMOTE_SSH) || \
    empty (LOCAL_ADDR) || \
    empty (REMOTE_ADDR) || \
    empty (FAKE_NET) || \
    empty (FAKE_NET_ADDR)
.BEGIN:
	@true
regress:
	@echo This tests needs a remote machine to operate on
	@echo LOCAL_IF REMOTE_SSH LOCAL_ADDR REMOTE_ADDR FAKE_NET FAKE_NET_ADDR
	@echo are empty.  Fill out these variables for additional tests.
	@echo SKIPPED
.endif

.MAIN: all

.if make (regress) || make (all)
.BEGIN:
	${SUDO} true
	ssh -t ${REMOTE_SSH} ${SUDO} true
	rm -f stamp-pfctl
	@echo
.endif

# Create python include file containing the addresses.
addr.py: Makefile
	rm -f $@ $@.tmp
	echo 'LOCAL_IF = "${LOCAL_IF}"' >>$@.tmp
.for var in LOCAL REMOTE FAKE_NET
	echo '${var}_ADDR = "${${var}_ADDR}"' >>$@.tmp
.endfor
.for var in FAKE_NET REMOTE_SSH
	echo '${var} = "${${var}}"' >>$@.tmp
.endfor
	echo 'CURDIR = "${.CURDIR}"' >>$@.tmp
	mv $@.tmp $@

# load the pf rules into the kernel of the REMOTE and LOCAL machine
REGRESS_SETUP +=	stamp-pfctl
stamp-pfctl: addr.py pf.conf
	cat addr.py ${.CURDIR}/pf.conf | /sbin/pfctl -n -f -
	cat addr.py ${.CURDIR}/pf.conf | ${SUDO} pfctl -a regress -f -
	cat addr.py ${.CURDIR}/pf.conf | \
	    ssh ${REMOTE_SSH} ${SUDO} pfctl -a regress -f -
	@date >$@

# Set variables so that make runs with and without obj directory.
# Only do that if necessary to keep visible output short.
.if ${.CURDIR} == ${.OBJDIR}
PYTHON =	python3 -u ./
.else
PYTHON =	PYTHONPATH=${.OBJDIR} python3 -u ${.CURDIR}/
.endif

REGRESS_TARGETS =
TCP_SCRIPTS !!=	cd ${.CURDIR} && ls -1 tcp*.py

# keepalive is special, it needs sysctl to see effect within a few seconds
run-tcp_keepalive: tcp_keepalive.py addr.py
	ssh ${REMOTE_SSH} ${SUDO} sysctl \
	    net.inet.tcp.keepidle=3 net.inet.tcp.keepintvl=1
	${SUDO} ${PYTHON}tcp_keepalive.py
	ssh ${REMOTE_SSH} ${SUDO} sysctl \
	    net.inet.tcp.keepidle=7200 net.inet.tcp.keepintvl=75

.for t in ${TCP_SCRIPTS:R}
REGRESS_TARGETS +=	run-$t
run-$t: $t.py addr.py
	${SUDO} ${PYTHON}$t.py
.endfor

.for t in CLOSING ESTABLISHED FIN_WAIT_1 FIN_WAIT_2 LAST_ACK SYN_RCVD SYN_SENT
REGRESS_TARGETS +=	run-netstat-${t:L:S/_//g}
netstat-${t:L:S/_//g}.log:
	${MAKE} -C ${.CURDIR} run-tcp_${t:L:S/_//g:C/[12]//}
run-netstat-${t:L:S/_//g}: netstat-${t:L:S/_//g}.log
	grep $t netstat-${t:L:S/_//g}.log
.endfor

.if ! empty(PF_ANCHOR:Mregress)
REGRESS_CLEANUP +=	cleanup-pf
cleanup-pf:
	${SUDO} pfctl -a regress -Fr
	ssh ${REMOTE_SSH} ${SUDO} pfctl -a regress -Fa
	rm -f stamp-pfctl
.endif

REGRESS_CLEANUP +=	cleanup-sysctl
cleanup-sysctl:
	ssh ${REMOTE_SSH} ${SUDO} sysctl net.inet.tcp.keepinittime=75 \
	    net.inet.tcp.keepidle=7200 net.inet.tcp.keepintvl=75

CLEANFILES +=		addr.py *.pyc *.log stamp-*

.PHONY: check-setup check-setup-local check-setup-remote

# Check wether the address, route and remote setup is correct
check-setup: check-setup-local check-setup-remote

check-setup-local:
	@echo '\n======== $@ ========'
	ping -n -c 1 ${LOCAL_ADDR}  # LOCAL_ADDR
	route -n get -inet ${LOCAL_ADDR} | grep -q 'flags: .*LOCAL'  # LOCAL_ADDR
	ping -n -c 1 ${REMOTE_ADDR}  # REMOTE_ADDR
	route -n get -inet ${REMOTE_ADDR} | fgrep -q 'interface: ${LOCAL_IF}'  # REMOTE_ADDR LOCAL_IF
	! ping -n -c 1 -w 1 ${FAKE_NET_ADDR}  # FAKE_NET_ADDR
.for ip in FAKE_NET FAKE_NET_ADDR
	route -n get -inet ${${ip}} | grep -q 'flags: .*BLACKHOLE'  # ${ip}
.endfor
	${SUDO} pfctl -sr | grep '^anchor "regress" all$$'

check-setup-remote:
	@echo '\n======== $@ ========'
	ssh ${REMOTE_SSH} ping -n -c 1 ${REMOTE_ADDR}  # REMOTE_ADDR
	ssh ${REMOTE_SSH} route -n get -inet ${REMOTE_ADDR} | grep -q 'flags: .*LOCAL'  # REMOTE_ADDR
	ssh ${REMOTE_SSH} ping -n -c 1 ${LOCAL_ADDR}  # LOCAL_ADDR
.for ip in FAKE_NET FAKE_NET_ADDR
	ssh ${REMOTE_SSH} route -n get -inet ${${ip}} | fgrep -q 'gateway: ${LOCAL_ADDR}'  # ${ip} LOCAL_ADDR
.endfor
	# useful for multiple tests: echo discard daytime chargen
	ssh ${REMOTE_SSH} netstat -na -f inet -p tcp | fgrep ' *.7 '
	ssh ${REMOTE_SSH} netstat -na -f inet -p tcp | fgrep ' *.9 '
	ssh ${REMOTE_SSH} netstat -na -f inet -p tcp | fgrep ' *.13 '
	ssh ${REMOTE_SSH} netstat -na -f inet -p tcp | fgrep ' *.19 '
	ssh ${REMOTE_SSH} ${SUDO} pfctl -sr | grep '^anchor "regress" all$$'
	ssh ${REMOTE_SSH} ${SUDO} pfctl -si | grep '^Status: Enabled '

.include <bsd.regress.mk>
