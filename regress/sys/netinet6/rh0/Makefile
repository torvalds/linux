#	$OpenBSD: Makefile,v 1.16 2020/12/30 21:40:33 kn Exp $

# The following ports must be installed:
#
# scapy			powerful interactive packet manipulation in python

.if ! exists(/usr/local/bin/scapy)
.BEGIN:
	@true
regress:
	@echo Install scapy package to run this regress.
	@echo SKIPPED
.endif

# This test needs a manual setup of two machines
# Set up machines: LOCAL REMOTE
# LOCAL is the machine where this makefile is running.
# REMOTE is running OpenBSD, pf gets disabled to test the IPv6 stack.
# OTHER is an address on REMOTE, but configured on another interface.
# OTHER_FAKE source routed host, no packets reach this host,
#     it represents just bunch of addresses in the OTHER net.

# Configure Addresses on the machines.
# Adapt interface and address variables to your local setup.

LOCAL_IF ?=	em1
LOCAL_MAC ?=	00:1b:21:0e:6e:8e
REMOTE_MAC ?=	00:04:23:b0:68:8e

LOCAL_ADDR6 ?=	fdd7:e83e:66bc:81::21
REMOTE_ADDR6 ?=	fdd7:e83e:66bc:81::22
OTHER_ADDR6 ?=	fdd7:e83e:66bc:82::22
OTHER_FAKE1_ADDR6 ?=	fdd7:e83e:66bc:82::dead
OTHER_FAKE2_ADDR6 ?=	fdd7:e83e:66bc:82::beef

REMOTE_SSH ?=

.if empty (LOCAL_IF) || empty (LOCAL_MAC) || empty (REMOTE_MAC) || \
    empty (LOCAL_ADDR6) || empty (REMOTE_ADDR6) || empty(OTHER_ADDR6) || \
    empty (OTHER_FAKE1_ADDR6) || empty (OTHER_FAKE2_ADDR6) || \
    empty (REMOTE_SSH)
.BEGIN:
	@true
regress:
	@echo This tests needs a remote machine to operate on.
	@echo LOCAL_IF LOCAL_MAC REMOTE_MAC LOCAL_ADDR6 REMOTE_ADDR6
	@echo OTHER_ADDR6 OTHER_FAKE1_ADDR6 OTHER_FAKE2_ADDR6 REMOTE_SSH
	@echo Fill out these variables for additional tests.
	@echo SKIPPED
.endif

.MAIN: all

.if make (regress) || make (all)
.BEGIN:
	${SUDO} true
	ssh -t ${REMOTE_SSH} ${SUDO} true
	rm -f stamp-stack stamp-pf
	@echo
.endif

# Create python include file containing the addresses.
addr.py: Makefile
	rm -f $@ $@.tmp
	echo 'LOCAL_IF = "${LOCAL_IF}"' >>$@.tmp
	echo 'LOCAL_MAC = "${LOCAL_MAC}"' >>$@.tmp
	echo 'REMOTE_MAC = "${REMOTE_MAC}"' >>$@.tmp
.for var in LOCAL_ADDR REMOTE_ADDR OTHER_FAKE1_ADDR OTHER_FAKE2_ADDR
	echo '${var}6 = "${${var}6}"' >>$@.tmp
.endfor
	mv $@.tmp $@

# Set variables so that make runs with and without obj directory.
# Only do that if necessary to keep visible output short.
.if ${.CURDIR} == ${.OBJDIR}
PYTHON =	python3 -u ./
.else
PYTHON =	PYTHONPATH=${.OBJDIR} python3 -u ${.CURDIR}/
.endif

stamp-stack:
	rm -f stamp-stack stamp-pf
	-ssh ${REMOTE_SSH} ${SUDO} pfctl -d
	ssh ${REMOTE_SSH} ${SUDO} pfctl -a regress -Fr
	date >$@

# After running the tests, turn on pf on remote machine.
# This is the expected default configuration.
REGRESS_CLEANUP +=      stamp-pf
stamp-pf:
	rm -f stamp-stack stamp-pf
	echo 'pass proto tcp from port ssh no state\n'\
	    'pass proto tcp to port ssh no state'|\
	    ssh ${REMOTE_SSH} ${SUDO} pfctl -a regress -f -
	-ssh ${REMOTE_SSH} ${SUDO} pfctl -e
	date >$@

RH0_SCRIPTS !!=		cd ${.CURDIR} && ls -1 rh0*.py

.for s in ${RH0_SCRIPTS}
REGRESS_TARGETS =	run-${s}
run-${s}: addr.py stamp-stack
	${SUDO} ${PYTHON}${s}
.endfor

CLEANFILES +=		addr.py *.pyc *.log stamp-*

.PHONY: check-setup check-setup-local check-setup-remote

# Check wether the address, route and remote setup is correct
check-setup: check-setup-local check-setup-remote

check-setup-local:
	@echo '\n======== $@ ========'
	ping6 -n -c 1 ${LOCAL_ADDR6}  # LOCAL_ADDR6
	route -n get -inet6 ${LOCAL_ADDR6} |\
	    grep -q 'flags: .*LOCAL'  # LOCAL_ADDR6
	ping6 -n -c 1 ${REMOTE_ADDR6}  # REMOTE_ADDR6
	route -n get -inet6 ${REMOTE_ADDR6} |\
	    grep -q 'interface: ${LOCAL_IF}$$'  # REMOTE_ADDR6 LOCAL_IF
.for ip in OTHER_ADDR6 OTHER_FAKE1_ADDR6 OTHER_FAKE2_ADDR6
	route -n get -inet6 ${${ip}} |\
	    grep -q 'gateway: ${REMOTE_ADDR6}$$'  # ${ip} REMOTE_ADDR6
.endfor
	ndp -n ${REMOTE_ADDR6} |\
	    grep -q ' ${REMOTE_MAC} '  # REMOTE_ADDR6 REMOTE_MAC

check-setup-remote:
	@echo '\n======== $@ ========'
	ssh ${REMOTE_SSH} ping6 -n -c 1 ${REMOTE_ADDR6}  # REMOTE_ADDR6
	ssh ${REMOTE_SSH} route -n get -inet6 ${REMOTE_ADDR6} |\
	    grep -q 'flags: .*LOCAL'  # REMOTE_ADDR6
	ssh ${REMOTE_SSH} ping6 -n -c 1 ${LOCAL_ADDR6}  # LOCAL_ADDR6
	ssh ${REMOTE_SSH} ping6 -n -c 1 ${OTHER_ADDR6}  # OTHER_ADDR6
	ssh ${REMOTE_SSH} route -n get -inet6 ${OTHER_ADDR6} |\
	    grep -q 'flags: .*LOCAL'  # OTHER_ADDR6
.for ip in OTHER_FAKE1_ADDR6 OTHER_FAKE2_ADDR6
	ssh ${REMOTE_SSH} route -n get -inet6 ${${ip}} |\
	    grep -q 'if address: ${OTHER_ADDR6}$$'  # ${ip} OTHER_ADDR6
.endfor
	ssh ${REMOTE_SSH} ndp -n ${LOCAL_ADDR6} |\
	    grep -q ' ${LOCAL_MAC} '  # LOCAL_ADDR6 LOCAL_MAC

.include <bsd.regress.mk>
