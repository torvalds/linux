#	$OpenBSD: Makefile,v 1.2 2018/10/08 20:42:14 bluhm Exp $

PROG=	cmsgsize

# LOCAL is the machine where this makefile is running
# FAKE is a non existing machine in the same network

LOCAL_ADDR ?=
FAKE_ADDR ?=

.if empty (LOCAL_ADDR) || empty (FAKE_ADDR)
regress:
	@echo This tests needs a local address and a non existing remote
	@echo address in the same IPv4 network.  LOCAL_ADDR FAKE_ADDR are
	@echo empty.  Fill out these variables for additional tests.
	@echo SKIPPED
.endif

run-regress-${PROG}: ${PROG}
	./${PROG} ${LOCAL_ADDR} ${FAKE_ADDR}

.include <bsd.regress.mk>
