#	$OpenBSD: Makefile,v 1.11 2023/10/19 18:36:40 anton Exp $

# The following ports must be installed:
#
# scapy               powerful interactive packet manipulation in python

RTABLE		?=	1
PAIR1		?=	pair1
PAIR2		?=	pair2
CTR_SOCK	?=	/dev/slaacd.sock

.if ! (make(clean) || make(cleandir) || make(obj))
# Check whether all required python packages are installed.  If some
# are missing print a warning and skip the tests, but do not fail.
AUTOCONF6 != sh -c "ifconfig | fgrep AUTOCONF6 2>&1" || true
INTERFACES != sh -c "ifconfig ${PAIR1} 2>/dev/null; \
    ifconfig ${PAIR2} 2>/dev/null" || true
.endif

.if ! exists(/usr/local/bin/scapy)
regress:
	@echo Install scapy package to run this regress.
	@echo SKIPPED
.endif

.if ! empty(AUTOCONF6)
regress:
	@echo 'AUTOCONF6 interface(s) already present'
	@echo SKIPPED
.endif
.if ! empty(INTERFACES)
regress:
	@echo 'pair(4) interfaces already present'
	@echo SKIPPED
.endif

# Set variables so that make runs with and without obj directory.
# Only do that if necessary to keep visible output short.
.if ${.CURDIR} == ${.OBJDIR}
PYTHON =	python3 -B -u ./
.else
PYTHON =	python3 -B -u ${.CURDIR}/
.endif

REGRESS_SETUP +=	setup
setup: cleanup
	${SUDO} ifconfig ${PAIR1} rdomain ${RTABLE} 10.11.12.1/24 up
	${SUDO} ifconfig ${PAIR2} rdomain ${RTABLE} 10.11.12.2/24 up
	${SUDO} ifconfig ${PAIR1} rdomain ${RTABLE} patch ${PAIR2}
	${SUDO} ifconfig ${PAIR1} inet6 rdomain ${RTABLE} eui64
	${SUDO} ifconfig ${PAIR2} inet6 rdomain ${RTABLE} eui64
	${SUDO} ifconfig ${PAIR2} inet6 rdomain ${RTABLE} autoconf
	${SUDO} route -nq -T ${RTABLE} add -host default 10.11.12.1 -reject

REGRESS_CLEANUP +=	cleanup
cleanup:
.for iface in ${PAIR1} ${PAIR2} lo${RTABLE}
	${SUDO} ifconfig ${iface} destroy 2>/dev/null || true
.endfor

REGRESS_TARGETS +=	run-send-solicitation
run-send-solicitation:
	${SUDO} route -T${RTABLE} exec ${PYTHON}sniff_sol.py ${CTR_SOCK}


REGRESS_TARGETS +=	run-parse-ra
run-parse-ra:
	${SUDO} route -T${RTABLE} exec ${PYTHON}process_ra.py \
	    ${PAIR1} ${PAIR2} ${CTR_SOCK}

.include <bsd.regress.mk>
