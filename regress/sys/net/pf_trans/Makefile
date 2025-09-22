#	$OpenBSD: Makefile,v 1.4 2025/07/31 19:51:06 bluhm Exp $

PROGS+=	dev-limit
PROGS+=	iocmd-limit

CFLAGS+=	-Wall

REGRESS_ROOT_TARGETS=	run-regress-dev-limit \
			run-regress-iocmd-limit

TIMEOUT?=	10

.include <bsd.own.mk>

.if ! (make(clean) || make(cleandir) || make(obj))

PF_STATUS !=	${SUDO} /sbin/pfctl -si | sed -n 's/^Status: \([^ ]*\) .*/\1/p'
.if empty(PF_STATUS:MEnabled)
regress:
	@echo pf status: "${PF_STATUS}"
	@echo Enable pf to run this regress.
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

PF_PREOPEN !=	fstat /dev/pf | grep -c ' pf ' || true
PF_MAXOPEN !=	expr 1023 - ${PF_PREOPEN}

.endif

#
# Create 2048 processes. Each child process will attempt
# open /dev/pf and wait for 10 secs on success. If
# /dev/pf can not be opened child exits immediately.
# There should be 1023 children, which could open /dev/pf.
# snmpd might keep /dev/pf open that must be subtracted
#
run-regress-dev-limit:
	${SUDO} ./dev-limit -c 2048 -s ${PF_MAXOPEN} -t ${TIMEOUT}

#
# Open 1024 tickets for DIOCGETRULES without closing them.
# Program expects to see EBUSY and returns 0 in that case.
#
# Open and close 1024 tickets. Program closes ticket by
# DIOCXEND before issuing next DIOCGETRULES command.
# Program expects to see no error and returns 0 in tat case.
#
run-regress-iocmd-limit:
	${SUDO} ./iocmd-limit -c DIOCGETRULES -i 513
	${SUDO} ./iocmd-limit -c DIOCXEND -i 1024

.include <bsd.regress.mk>
