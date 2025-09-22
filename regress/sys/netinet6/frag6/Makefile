#	$OpenBSD: Makefile,v 1.30 2024/03/12 21:31:29 bluhm Exp $

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
# Enable echo udp6 in inetd.conf on REMOTE to test UDP fragments.
# REMOTE_SSH is used to login and enable or disable pf automatically.

# Configure addresses on the machines.
# Adapt interface and addresse variables to your local setup.

LOCAL_IF ?=	em1
LOCAL_MAC ?=	00:1b:21:0e:6e:8e
REMOTE_MAC ?=	00:04:23:b0:68:8e

LOCAL_ADDR6 ?=	fdd7:e83e:66bc:81::21
REMOTE_ADDR6 ?=	fdd7:e83e:66bc:81::22

REMOTE_SSH ?=

.if empty (LOCAL_IF) || empty (LOCAL_MAC) || empty (REMOTE_MAC) || \
    empty (LOCAL_ADDR6) || empty (REMOTE_ADDR6) || empty (REMOTE_SSH)
.BEGIN:
	@true
regress:
	@echo This tests needs a remote machine to operate on.
	@echo LOCAL_IF LOCAL_MAC REMOTE_MAC LOCAL_ADDR6 REMOTE_ADDR6 REMOTE_SSH
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

stamp-pf: addr.py pf.conf
	rm -f stamp-stack stamp-pf
	cat addr.py ${.CURDIR}/pf.conf | pfctl -n -f -
	cat addr.py ${.CURDIR}/pf.conf | \
	    ssh ${REMOTE_SSH} ${SUDO} pfctl -a regress -f -
	-ssh ${REMOTE_SSH} ${SUDO} pfctl -e
	date >$@

FRAG6_SCRIPTS !!=	cd ${.CURDIR} && ls -1 frag6*.py

run-stack-frag6_queuelimit.py:
	# stack does not limit the amount of fragments during reassembly
	@echo DISABLED

run-pf-frag6_oversize.py run-pf-frag6_unfragsize.py:
	# pf does not send icmp parameter problem, so test does not work
	@echo DISABLED

run-stack-frag6_doubleatomic.py: addr.py stamp-stack
	# IPv6 stack accepts double atomic fragement, this is not a big issue
	set +e; ${SUDO} ${PYTHON}frag6_doubleatomic.py; [[ $$? == 1 ]]

.for sp in stack pf

# Ping all addresses.  This ensures that the ip addresses are configured
# and all routing table are set up to allow bidirectional packet flow.
${sp}: run-${sp}-ping6
REGRESS_TARGETS +=	run-${sp}-ping6
run-${sp}-ping6: stamp-${sp}
.for ip in LOCAL_ADDR REMOTE_ADDR
	@echo Check ping6 ${ip}6:
	ping6 -n -c 1 ${${ip}6}
.endfor

# Ping all addresses again but with 5000 bytes payload.  These large
# packets get fragmented by LOCAL and must be handled by REMOTE.
${sp}: run-${sp}-fragping6
REGRESS_TARGETS +=	run-${sp}-fragping6
run-${sp}-fragping6: stamp-${sp}
.for ip in LOCAL_ADDR REMOTE_ADDR
	@echo Check ping6 ${ip}6:
	ping6 -n -c 1 -s 5000 -m ${${ip}6}
.endfor

.for s in ${FRAG6_SCRIPTS}
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
	ping6 -n -c 1 ${LOCAL_ADDR6}  # LOCAL_ADDR6
	route -n get -inet6 ${LOCAL_ADDR6} |\
	    grep -q 'flags: .*LOCAL'  # LOCAL_ADDR6
	ping6 -n -c 1 ${REMOTE_ADDR6}  # REMOTE_ADDR6
	route -n get -inet6 ${REMOTE_ADDR6} |\
	    grep -q 'interface: ${LOCAL_IF}$$'  # REMOTE_ADDR6 LOCAL_IF
	ndp -n ${REMOTE_ADDR6} |\
	    grep -q ' ${REMOTE_MAC} '  # REMOTE_ADDR6 REMOTE_MAC

check-setup-remote:
	@echo '\n======== $@ ========'
	ssh ${REMOTE_SSH} ping6 -n -c 1 ${REMOTE_ADDR6}  # REMOTE_ADDR6
	ssh ${REMOTE_SSH} route -n get -inet6 ${REMOTE_ADDR6} |\
	    grep -q 'flags: .*LOCAL'  # REMOTE_ADDR6
	ssh ${REMOTE_SSH} ping6 -n -c 1 ${LOCAL_ADDR6}  # LOCAL_ADDR6
	ssh ${REMOTE_SSH} ndp -n ${LOCAL_ADDR6} |\
	    grep -q ' ${LOCAL_MAC} '  # LOCAL_ADDR6 LOCAL_MAC
	ssh ${REMOTE_SSH} route -n get -inet6 ${FAKE_NET_ADDR6} |\
	    grep -q 'gateway: ${LOCAL_ADDR6}'  # FAKE_NET_ADDR6 LOCAL_ADDR6
	ssh ${REMOTE_SSH} netstat -na -f inet6 -p udp | fgrep ' *.7 '

.include <bsd.regress.mk>
