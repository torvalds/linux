#	$OpenBSD: Makefile,v 1.11 2021/05/22 18:38:40 bluhm Exp $

# The following ports must be installed:
#
# scapy               powerful interactive packet manipulation in python

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
# REMOTE is running OpenBSD with or without pf to test fragment reassemly
# Enable echo udp in inetd.conf on REMOTE to test UDP fragments.
# REMOTE_SSH is used to login and enable or disable pf automatically.

# Configure addresses on the machines.
# Adapt interface and addresse variables to your local setup.

LOCAL_IF ?=	em1
LOCAL_MAC ?=	00:1b:21:0e:6e:8e
REMOTE_MAC ?=	00:04:23:b0:68:8e

LOCAL_ADDR ?=	10.188.81.21
REMOTE_ADDR ?=	10.188.81.22

REMOTE_SSH ?=

.if empty (LOCAL_IF) || empty (LOCAL_MAC) || empty (REMOTE_MAC) || \
    empty (LOCAL_ADDR) || empty (REMOTE_ADDR) || empty (REMOTE_SSH)
.BEGIN:
	@true
regress:
	@echo This tests needs a remote machine to operate on.
	@echo LOCAL_IF LOCAL_MAC REMOTE_MAC LOCAL_ADDR REMOTE_ADDR REMOTE_SSH
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
.for var in LOCAL_ADDR REMOTE_ADDR
	echo '${var} = "${${var}}"' >>$@.tmp
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

stamp-pf: addr.py pf.conf
	rm -f stamp-stack stamp-pf
	cat addr.py ${.CURDIR}/pf.conf | /sbin/pfctl -n -f -
	cat addr.py ${.CURDIR}/pf.conf | \
	    ssh ${REMOTE_SSH} ${SUDO} pfctl -a regress -f -
	-ssh ${REMOTE_SSH} ${SUDO} pfctl -e
	date >$@

REGRESS_TARGETS =
FRAG_SCRIPTS !!=	cd ${.CURDIR} && ls -1 frag*.py

run-stack-frag_maxpacket0.py:
	# the stack allows fragments with data behind maximum packt length
	@echo DISABLED

run-stack-frag_mf0long.py:
	# the stack allows fragments with data behind a fragment without MF
	@echo DISABLED

run-stack-frag_mf0short.py:
	# the stack allows fragments without MF ending before other fragments
	@echo DISABLED

run-stack-frag_mf1end.py:
	# the stack allows fragments with MF together with fragments without MF
	@echo DISABLED

run-stack-frag_queuelimit.py run-stack-frag_overlimit.py:
	# the stack does not limit the amount of fragments during reassembly
	@echo DISABLED

.for sp in stack pf

# Ping all addresses.  This ensures that the ip addresses are configured
# and all routing table are set up to allow bidirectional packet flow.
${sp}: run-${sp}-ping
REGRESS_TARGETS +=	run-${sp}-ping
run-${sp}-ping: stamp-${sp}
.for ip in LOCAL_ADDR REMOTE_ADDR
	@echo Check ping ${ip}:
	ping -n -c 1 ${${ip}}
.endfor

# Ping all addresses again but with 5000 bytes payload.  These large
# packets get fragmented by LOCAL and must be handled by REMOTE.
${sp}: run-${sp}-fragping
REGRESS_TARGETS +=	run-${sp}-fragping
run-${sp}-fragping: stamp-${sp}
.for ip in LOCAL_ADDR REMOTE_ADDR
	@echo Check ping ${ip}:
	ping -n -c 1 -s 5000 ${${ip}}
.endfor

.for s in ${FRAG_SCRIPTS}
${sp}: run-${sp}-${s}
REGRESS_TARGETS +=	run-${sp}-${s}
run-${sp}-${s}: addr.py stamp-${sp}
	${SUDO} ${PYTHON}${s}
.endfor

.endfor

# After running the tests, turn on pf on remote machine.
# This is the expected default configuration.
REGRESS_CLEANUP +=	cleanup-pf
cleanup-pf:
	rm -f stamp-stack stamp-pf
	ssh ${REMOTE_SSH} ${SUDO} pfctl -a regress -Fa
	-ssh ${REMOTE_SSH} ${SUDO} pfctl -e || true

CLEANFILES +=		addr.py *.pyc *.log stamp-*

.PHONY: check-setup check-setup-local check-setup-remote

# Check wether the address, route and remote setup is correct
check-setup: check-setup-local check-setup-remote

check-setup-local:
	@echo '\n======== $@ ========'
	ping -n -c 1 ${LOCAL_ADDR}  # LOCAL_ADDR
	route -n get -inet ${LOCAL_ADDR} |\
	    grep -q 'flags: .*LOCAL'  # LOCAL_ADDR
	ping -n -c 1 ${REMOTE_ADDR}  # REMOTE_ADDR
	route -n get -inet ${REMOTE_ADDR} |\
	    grep -q 'interface: ${LOCAL_IF}$$'  # REMOTE_ADDR LOCAL_IF
	arp -n ${REMOTE_ADDR} |\
	    grep -q ' ${REMOTE_MAC} '  # REMOTE_ADDR REMOTE_MAC

check-setup-remote:
	@echo '\n======== $@ ========'
	ssh ${REMOTE_SSH} ping -n -c 1 ${REMOTE_ADDR}  # REMOTE_ADDR
	ssh ${REMOTE_SSH} route -n get -inet ${REMOTE_ADDR} |\
	    grep -q 'flags: .*LOCAL'  # REMOTE_ADDR
	ssh ${REMOTE_SSH} ping -n -c 1 ${LOCAL_ADDR}  # LOCAL_ADDR
	ssh ${REMOTE_SSH} arp -n ${LOCAL_ADDR} |\
	    grep -q ' ${LOCAL_MAC} '  # LOCAL_ADDR LOCAL_MAC

.include <bsd.regress.mk>
