#	$OpenBSD: Makefile,v 1.11 2023/05/24 20:31:49 bluhm Exp $

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

# The following ports must be installed:
#
# scapy			powerful interactive packet manipulation in python
# p5-BSD-Socket-Splice	perl interface to OpenBSD socket splicing
# p5-Socket6		Perl defines relating to AF_INET6 sockets

.if ! (make(clean) || make(cleandir) || make(obj))

.if ! exists(/usr/local/bin/scapy)
regress:
	@echo Install scapy package to run this regress.
	@echo SKIPPED
.endif

PERL_REQUIRE !=	perl -Mstrict -Mwarnings -e ' \
    eval { require BSD::Socket::Splice } or print $@; \
    eval { require Socket6 } or print $@; \
'
.if ! empty (PERL_REQUIRE)
regress:
	@echo "${PERL_REQUIRE}"
	@echo Install these Perl modules for additional tests.
	@echo SKIPPED
.endif

PF_STATUS !=	${SUDO} pfctl -si | sed -n 's/^Status: \([^ ]*\) .*/\1/p'
PF_ANCHOR !=	${SUDO} pfctl -sr | sed -n 's/^anchor "\([^"]*\)" all$$/\1/p'
.if ! empty(PF_STATUS:MEnabled) && empty(PF_ANCHOR:Mregress)
regress:
	@echo pf status: "${PF_STATUS}"
	@echo pf anchor: "${PF_ANCHOR}"
	@echo This regress cannot run with pf filtering packets on localhost.
	@echo Either disable pf or add a line 'anchor "regress"' to pf.conf.
	@echo There the test will add a stateless rule that passes all packets.
	@echo SKIPPED
.endif

.endif

# This test needs a manual setup of two machines
# Set up machines: LOCAL REMOTE
# LOCAL is the machine where this makefile is running.
# REMOTE is running OpenBSD with echo server to test PMTU
# FAKE is an non existing machine in a non existing network.
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
	ssh -t ${REMOTE_SSH} true
	@echo
.endif

# Create python include file containing the addresses.
addr.py: Makefile
	rm -f $@ $@.tmp
	echo 'LOCAL_IF = "${LOCAL_IF}"' >>$@.tmp
.for var in LOCAL REMOTE FAKE_NET
	echo '${var}_ADDR = "${${var}_ADDR}"' >>$@.tmp
.endfor
	echo 'FAKE_NET = "${FAKE_NET}"' >>$@.tmp
	mv $@.tmp $@

# load the pf rules into the kernel of the REMOTE and LOCAL machine
REGRESS_SETUP +=	stamp-pfctl
stamp-pfctl:
.if ! empty(PF_ANCHOR:Mregress)
	echo 'pass on ${LOCAL_IF} no state' | ${SUDO} pfctl -a regress -f -
.endif
	@date >$@

SCAPYS !=		cd ${.CURDIR} && ls scapy-*.py
TARGETS =

# Set variables so that make runs with and without obj directory.
# Only do that if necessary to keep visible output short.
.if ${.CURDIR} == ${.OBJDIR}
PYTHON =	python3 -u
SCAPYPATH =
.else
PYTHON =	PYTHONPATH=${.OBJDIR} python3 -u
SCAPYPATH =	${.CURDIR}/
.endif

.for s in ${SCAPYS}
REGRESS_TARGETS +=	run-$s
run-$s: $s addr.py
	time SUDO="${SUDO}" PYTHON="${PYTHON}" perl ${PERLINC} ${PERLPATH}scapy.pl splice ${FAKE_NET_ADDR} ${REMOTE_ADDR} ${REMOTE_SSH} ${SCAPYPATH}$s
.endfor

.if ! empty(PF_ANCHOR:Mregress)
REGRESS_CLEANUP +=	cleanup
cleanup:
	${SUDO} pfctl -a regress -Fr
	rm -f stamp-pfctl
.endif

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

check-setup-remote:
	@echo '\n======== $@ ========'
	ssh ${REMOTE_SSH} ping -n -c 1 ${REMOTE_ADDR}  # REMOTE_ADDR
	ssh ${REMOTE_SSH} route -n get -inet ${REMOTE_ADDR} | grep -q 'flags: .*LOCAL'  # REMOTE_ADDR
	ssh ${REMOTE_SSH} ping -n -c 1 ${LOCAL_ADDR}  # LOCAL_ADDR
.for ip in FAKE_NET FAKE_NET_ADDR
	ssh ${REMOTE_SSH} route -n get -inet ${${ip}} | fgrep -q 'gateway: ${LOCAL_ADDR}'  # ${ip} LOCAL_ADDR
.endfor

.include <bsd.regress.mk>
