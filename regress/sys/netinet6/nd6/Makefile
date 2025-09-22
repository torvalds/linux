#	$OpenBSD: Makefile,v 1.13 2020/12/30 21:40:33 kn Exp $

# The following ports must be installed:
#
# scapy               powerful interactive packet manipulation in python

.if ! exists(/usr/local/bin/scapy)
regress:
	@echo Install scapy package to run this regress.
	@echo SKIPPED
.endif

# This test needs a manual setup of two machines
# Set up machines: LOCAL REMOTE
# LOCAL is the machine where this makefile is running.
# REMOTE is running OpenBSD with pf to test the neighbor discovery states.

# Configure addresses on the machines.
# Adapt interface and addresse variables to your local setup.

LOCAL_IF ?=	em1
LOCAL_MAC ?=	00:1b:21:0e:6e:8e
REMOTE_MAC ?=	00:04:23:b0:68:8e

LOCAL_ADDR6 ?=	fdd7:e83e:66bc:81::21
REMOTE_ADDR6 ?=	fdd7:e83e:66bc:81::22

REMOTE_SSH ?=

# pf rules on REMOTE should look like this:
#
# block log
# pass inet6 proto icmp6 icmp6-type echoreq keep state
# pass inet6 proto icmp6 icmp6-type neighbrsol keep state
# pass inet6 proto icmp6 icmp6-type neighbradv keep state

# RFC 4861 7. describes the following test cases for ND:
#
# Duplicate Address Detection
# - request  NS from unspecified address to target solicitated-node multicast
# - response NA from interface address   to all-nodes multicast
#
# Address Resolution
# - request  NS from interface address   to target solicitated-node multicast
# - response NA from interface address   to source of NS
#
# Unsolicited Neighbor Advertisements
# - request  NA from interface address   to all-nodes multicast
#
# Neighbor Unreachability Detection
# - request  NS from interface address   to target unicast
# - response NA from interface address   to source of NS

.if empty (LOCAL_IF) || empty (LOCAL_MAC) || empty (REMOTE_MAC) || \
    empty (LOCAL_ADDR6) || empty (REMOTE_ADDR6) || empty (REMOTE_SSH)
regress:
	@echo This tests needs a remote machine to operate on.
	@echo LOCAL_IF LOCAL_MAC REMOTE_MAC LOCAL_ADDR6 REMOTE_ADDR6
	@echo REMOTE_SSH are empty.
	@echo Fill out these variables for additional tests.
	@echo SKIPPED
.endif

# Create python include file containing the addresses.
addr.py: Makefile
	rm -f $@ $@.tmp
	echo 'LOCAL_IF = "${LOCAL_IF}"' >>$@.tmp
	echo 'LOCAL_MAC = "${LOCAL_MAC}"' >>$@.tmp
	echo 'REMOTE_MAC = "${REMOTE_MAC}"' >>$@.tmp
.for var in LOCAL_ADDR REMOTE_ADDR
	echo '${var} = "${${var}}"' >>$@.tmp
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

# Clear neighbor cache and ping all addresses.  This ensures that
# the ip addresses are configured and all routing table are set up
# to allow bidirectional packet flow.
REGRESS_TARGETS +=	run-ping6
run-ping6:
	${SUDO} ndp -c
.for ip in LOCAL_ADDR REMOTE_ADDR
	@echo Check ping6 ${ip}6:
	ping6 -n -c 1 ${${ip}6}
.endfor

ND6_SCRIPTS !!=		cd ${.CURDIR} && ls -1 nd6*.py

.for s in ${ND6_SCRIPTS}
REGRESS_TARGETS +=	run-${s}
run-${s}: addr.py
	${SUDO} ${PYTHON}${s}
.endfor

CLEANFILES +=		addr.py *.pyc *.log

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

.include <bsd.regress.mk>
