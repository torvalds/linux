#	$OpenBSD: Makefile,v 1.15 2020/12/30 21:40:33 kn Exp $

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
# REMOTE is running OpenBSD with ARP to test the Address Resolution Protocol.
# FAKE is an non existing machine, its IP is used in the tests.
# OTHER is an IP on REMOTE, but configured on another interface.
# OTHER_FAKE is an non existing IP on another interface.
# REMOTE_SSH is the hostname to log in on the REMOTE machine.

# Configure Addresses on the machines.
# Adapt interface and addresse variables to your local setup.
#
LOCAL_IF ?=
LOCAL_MAC ?=
REMOTE_MAC ?=
FAKE_MAC ?= 12:34:56:78:9a:bc
PROXY_MAC ?= 00:90:27:bb:cc:dd
REMOTE_SSH ?=

LOCAL_ADDR ?= 10.188.70.17
REMOTE_ADDR ?= 10.188.70.70
FAKE_ADDR ?= 10.188.70.188
OTHER_ADDR ?= 10.188.211.70
OTHER_FAKE_ADDR ?= 10.188.211.188

.if empty (LOCAL_IF) || empty (LOCAL_MAC) || empty (REMOTE_MAC) || \
    empty (FAKE_MAC) || empty (REMOTE_SSH) || empty (LOCAL_ADDR) || \
    empty (REMOTE_ADDR) || empty (FAKE_ADDR) || empty (OTHER_ADDR) || \
    empty (OTHER_FAKE_ADDR)
regress:
	@echo this tests needs a remote machine to operate on
	@echo LOCAL_IF LOCAL_MAC REMOTE_MAC FAKE_MAC REMOTE_SSH LOCAL_ADDR
	@echo REMOTE_ADDR FAKE_ADDR OTHER_ADDR OTHER_FAKE_ADDR are empty
	@echo fill out these variables for additional tests
	@echo SKIPPED
.endif

.if ! empty (REMOTE_SSH)
.if make (regress) || make (all)
.BEGIN:
	${SUDO} true
	ssh -t ${REMOTE_SSH} ${SUDO} true
	@echo
.endif
.endif

# Create python include file containing the addresses.
addr.py: Makefile
	rm -f $@ $@.tmp
	echo 'LOCAL_IF = "${LOCAL_IF}"' >>$@.tmp
.for var in LOCAL REMOTE FAKE
	echo '${var}_MAC = "${${var}_MAC}"' >>$@.tmp
.endfor
.for var in LOCAL REMOTE FAKE OTHER OTHER_FAKE
	echo '${var}_ADDR = "${${var}_ADDR}"' >>$@.tmp
.endfor
	mv $@.tmp $@

# Set variables so that make runs with and without obj directory.
# Only do that if necessary to keep visible output short.
.if ${.CURDIR} == ${.OBJDIR}
PYTHON =	python3 -u ./
.else
PYTHON =	PYTHONPATH=${.OBJDIR} python3 -u ${.CURDIR}/
.endif

# Clear local and remote ARP cache.
REGRESS_SETUP +=	clean-arp
REGRESS_CLEANUP +=	clean-arp
clean-arp:
	${SUDO} arp -da
	ssh ${REMOTE_SSH} ${SUDO} arp -da

# Clear ARP cache and ping all addresses.  This ensures that
# the IP addresses are configured and all routing table are set up
# to allow bidirectional packet flow.
REGRESS_TARGETS +=	run-ping
run-ping:
.for ip in LOCAL_ADDR REMOTE_ADDR
	@echo Check ping ${ip}
	ping -n -c 1 ${${ip}}
.endfor

# Send an ARP request from the local machine, asking for the remote
# machine's MAC.  Target MAC is broadcast, Target IP is remote address.
# Check that all fields of the answer are filled out correctly.
# Check that the remote machine has the local IP and MAC in its ARP table.
REGRESS_TARGETS +=	run-arp-request
run-arp-request: addr.py
	@echo Send ARP Request for remote address and insert local address
	${SUDO} ${PYTHON}arp_request.py
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	grep '^${LOCAL_ADDR} .* ${LOCAL_MAC} ' arp.log

# Send an ARP request from the local machine, but use a multicast MAC
# as sender.  Although there is a special check in in_arpinput(),
# this must be answered.  The ARP entry on the remote machine for the
# local address is changed to the multicast MAC.
# Check that all fields of the answer are filled out correctly.
# Check that the remote machine overwrites the local address.
REGRESS_TARGETS +=	run-arp-multicast
run-arp-multicast: addr.py
	@echo Send ARP Request and overwrite entry with multicast ethernet
	ssh ${REMOTE_SSH} logger -t "arp-regress[$$$$]" $@
	ssh ${REMOTE_SSH} ${SUDO} arp -s ${LOCAL_ADDR} ${LOCAL_MAC} temp
	scp ${REMOTE_SSH}:/var/log/messages old.log
	${SUDO} ${PYTHON}arp_multicast.py
	scp ${REMOTE_SSH}:/var/log/messages new.log
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	ssh ${REMOTE_SSH} ${SUDO} arp -d ${LOCAL_ADDR}
	diff old.log new.log | grep '^> ' >diff.log
	grep 'bsd: arp info overwritten for ${LOCAL_ADDR} by 33:33:33:33:33:33' diff.log
	grep '^${LOCAL_ADDR} .* 33:33:33:33:33:33 ' arp.log

# Send an ARP probe from the local machine with the remote IP as
# target.  Sender MAC is local and IP is 0.  The remote machine must
# defend its IP address with an ARP reply.
# Check that all fields of the answer are filled out correctly.
REGRESS_TARGETS +=	run-arp-probe
run-arp-probe: addr.py
	@echo Send ARP Probe for existing address and expect correct reply
	${SUDO} ${PYTHON}arp_probe.py

# Send ARP request with broadcast MAC as sender.
# Check that no answer is received.
# Check that the remote machine rejects the broadcast sender.
REGRESS_TARGETS +=	run-arp-broadcast
run-arp-broadcast: addr.py
	@echo Send ARP Request with broadcast as sender hardware address
	ssh ${REMOTE_SSH} logger -t "arp-regress[$$$$]" $@
	scp ${REMOTE_SSH}:/var/log/messages old.log
	${SUDO} ${PYTHON}arp_broadcast.py
	scp ${REMOTE_SSH}:/var/log/messages new.log
	diff old.log new.log | grep '^> ' >diff.log
	grep 'bsd: arp: ether address is broadcast for IP address ${LOCAL_ADDR}' diff.log

# The local machine announces that it has taken the remote machine's
# IP.  The sender is the local machines MAC and the remote IP.  The
# remote machine must defend its IP address with an ARP reply.
# Check that all fields of the answer are filled out correctly.
# Check that the remote machine reports an duplicate address.
# Check that the remote machine keeps its local ARP entry.
REGRESS_TARGETS +=	run-arp-announcement
run-arp-announcement: addr.py
	@echo Send ARP Announcement for existing address
	ssh ${REMOTE_SSH} logger -t "arp-regress[$$$$]" $@
	scp ${REMOTE_SSH}:/var/log/messages old.log
	${SUDO} ${PYTHON}arp_announcement.py
	scp ${REMOTE_SSH}:/var/log/messages new.log
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	diff old.log new.log | grep '^> ' >diff.log
	grep 'bsd: duplicate IP address ${REMOTE_ADDR} sent from ethernet address ${LOCAL_MAC}' diff.log
	grep '^${REMOTE_ADDR} .* ${REMOTE_MAC} .* permanent * l$$' arp.log

# The local machine sends an gratuitous ARP reply for the remote IP
# with its local MAC.
# Check that no answer is received.
# Check that the remote machine reports an duplicate address.
# Check that the remote machine keeps its local ARP entry.
REGRESS_TARGETS +=	run-arp-gratuitous
run-arp-gratuitous: addr.py
	@echo Send Gratuitous ARP for existing address
	ssh ${REMOTE_SSH} logger -t "arp-regress[$$$$]" $@
	scp ${REMOTE_SSH}:/var/log/messages old.log
	${SUDO} ${PYTHON}arp_gratuitous.py
	scp ${REMOTE_SSH}:/var/log/messages new.log
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	diff old.log new.log | grep '^> ' >diff.log
	grep 'bsd: duplicate IP address ${REMOTE_ADDR} sent from ethernet address ${LOCAL_MAC}' diff.log
	grep '^${REMOTE_ADDR} .* ${REMOTE_MAC} .* permanent * l$$' arp.log

# Add a permanent entry on the remote machine for a fake MAC and IP.
# Send a request form the local machine, indicating with the local
# MAC and the fake IP as sender that it claims the fake address.
# Check that no answer is received.
# Check that the attempt to overwrite the permanent entry is logged.
# Check that the remote machine keeps its permanent ARP entry.
REGRESS_TARGETS +=	run-arp-permanent
run-arp-permanent: addr.py
	@echo Send ARP Request to change permanent fake address
	ssh ${REMOTE_SSH} logger -t "arp-regress[$$$$]" $@
	ssh ${REMOTE_SSH} ${SUDO} arp -s ${FAKE_ADDR} ${FAKE_MAC} permanent
	scp ${REMOTE_SSH}:/var/log/messages old.log
	${SUDO} ${PYTHON}arp_fake.py
	scp ${REMOTE_SSH}:/var/log/messages new.log
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	ssh ${REMOTE_SSH} ${SUDO} arp -d ${FAKE_ADDR}
	diff old.log new.log | grep '^> ' >diff.log
	grep 'bsd: arp: attempt to overwrite permanent entry for ${FAKE_ADDR} by ${LOCAL_MAC}' diff.log
	grep '^${FAKE_ADDR} .* ${FAKE_MAC} .* permanent * $$' arp.log

# The remote machine has a second address on another interface.
# The local machine claims this address in its sender IP.
# Check that no answer is received.
# Check that the attempt to overwrite the permanent entry is logged.
# Check that the remote machine keeps its local ARP entry.
REGRESS_TARGETS +=	run-arp-address
run-arp-address: addr.py
	@echo Send ARP Request to change address on other interface
	ssh ${REMOTE_SSH} logger -t "arp-regress[$$$$]" $@
	scp ${REMOTE_SSH}:/var/log/messages old.log
	${SUDO} ${PYTHON}arp_other.py
	scp ${REMOTE_SSH}:/var/log/messages new.log
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	diff old.log new.log | grep '^> ' >diff.log
	grep 'bsd: arp: attempt to overwrite permanent entry for ${OTHER_ADDR} by ${LOCAL_MAC}' diff.log
	grep '^${OTHER_ADDR} .* permanent * l$$' arp.log

# The remote machine has a second address on another interface.  Add
# a temporary ARP entry for a fake address in this network on the
# remote machine.  The local machine tries to overwrite this address
# with its own MAC.
# Check that no answer is received.
# Check that the attempt to overwrite the permanent entry is logged.
# Check that the remote machine keeps its ARP entry.
REGRESS_TARGETS +=	run-arp-temporary
run-arp-temporary: addr.py
	@echo Send ARP Request to change temporary entry on other interface
	ssh ${REMOTE_SSH} logger -t "arp-regress[$$$$]" $@
	ssh ${REMOTE_SSH} ${SUDO} arp -s ${OTHER_FAKE_ADDR} ${FAKE_MAC} temp
	scp ${REMOTE_SSH}:/var/log/messages old.log
	${SUDO} ${PYTHON}arp_otherfake.py
	scp ${REMOTE_SSH}:/var/log/messages new.log
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	ssh ${REMOTE_SSH} ${SUDO} arp -d ${OTHER_FAKE_ADDR}
	diff old.log new.log | grep '^> ' >diff.log
	grep 'bsd: arp: attempt to overwrite entry for ${OTHER_FAKE_ADDR} on .* by ${LOCAL_MAC} on .*' diff.log
	grep '^${OTHER_FAKE_ADDR} .* ${FAKE_MAC} ' arp.log

# The remote machine has a second address on another interface.  Create
# an incomplete ARP entry for a fake address in this network on the
# remote machine with an unsuccessful ping.  The local machine tries
# to overwrite this address with its own MAC.
# Check that no answer is received.
# Check that the attempt to add an entry is logged.
# Check that the remote machine keeps its incomplete ARP entry.
REGRESS_TARGETS +=	run-arp-incomplete
run-arp-incomplete: addr.py
	@echo Send ARP Request filling an incomplete entry on other interface
	ssh ${REMOTE_SSH} logger -t "arp-regress[$$$$]" $@
	ssh ${REMOTE_SSH} ${SUDO} ping -n -w 1 -c 1 ${OTHER_FAKE_ADDR} || true
	scp ${REMOTE_SSH}:/var/log/messages old.log
	${SUDO} ${PYTHON}arp_otherfake.py
	scp ${REMOTE_SSH}:/var/log/messages new.log
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	ssh ${REMOTE_SSH} ${SUDO} arp -d ${OTHER_FAKE_ADDR}
	diff old.log new.log | grep '^> ' >diff.log
	grep 'bsd: arp: attempt to add entry for ${OTHER_FAKE_ADDR} on .* by ${LOCAL_MAC} on .*' diff.log
	grep '^${OTHER_FAKE_ADDR} .* (incomplete) ' arp.log

# Publish a proxy ARP entry on the remote machine for a fake address.
# The local machine requests this IP as a the target.
# Check that all fields of the answer are filled out correctly.
# Check that the remote machine has a public ARP entry.
REGRESS_TARGETS +=	run-arp-proxy
run-arp-proxy: addr.py
	@echo Send ARP Request for fake address that is proxied
	ssh ${REMOTE_SSH} ${SUDO} arp -s ${FAKE_ADDR} ${PROXY_MAC}
	ssh ${REMOTE_SSH} ${SUDO} arp -s ${FAKE_ADDR} ${FAKE_MAC} pub
	${SUDO} ${PYTHON}arp_proxy.py
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	ssh ${REMOTE_SSH} ${SUDO} arp -d ${FAKE_ADDR}
	ssh ${REMOTE_SSH} ${SUDO} arp -d ${FAKE_ADDR}
	grep '^${FAKE_ADDR} .* ${FAKE_MAC} .* static * p$$' arp.log

# Enter a static ARP entry on the remote machine for a fake address,
# but do not publish it.  The local machine requests this IP as a the
# target.
# Check that no answer is received.
# Check that the remote machine has a static ARP entry.
REGRESS_TARGETS +=	run-arp-nonproxy
run-arp-nonproxy: addr.py
	@echo Send ARP Request for fake address that is not published
	ssh ${REMOTE_SSH} ${SUDO} arp -s ${FAKE_ADDR} ${FAKE_MAC}
	${SUDO} ${PYTHON}arp_nonproxy.py
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	ssh ${REMOTE_SSH} ${SUDO} arp -d ${FAKE_ADDR}
	grep '^${FAKE_ADDR} .* ${FAKE_MAC} .* static * $$' arp.log

# Publish a proxy ARP entry on the remote machine for a fake address
# on another interface.  The local machine requests this IP.  As the
# proxy entry is for another interface, it must not be answered.
# Check that no answer is received.
# Check that the remote machine has a public ARP entry.
REGRESS_TARGETS +=	run-arp-otherproxy
run-arp-otherproxy: addr.py
	@echo Send ARP Request for address proxied on another interface
	ssh ${REMOTE_SSH} ${SUDO} arp -s ${OTHER_FAKE_ADDR} ${FAKE_MAC} pub
	${SUDO} ${PYTHON}arp_otherproxy.py
	ssh ${REMOTE_SSH} ${SUDO} arp -an >arp.log
	ssh ${REMOTE_SSH} ${SUDO} arp -d ${OTHER_FAKE_ADDR}
	grep '^${OTHER_FAKE_ADDR} .* ${FAKE_MAC} .* static * p$$' arp.log

CLEANFILES +=		addr.py *.pyc *.log

.include <bsd.regress.mk>
