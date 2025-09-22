# $OpenBSD: Makefile,v 1.1.1.1 2025/05/23 16:19:52 bluhm Exp $

PROG=		udpthread
LDADD=		-lpthread
DPADD=		${LIBPTHREAD}
WARNINGS=	yes

CLEANFILES=	ktrace.out

REGRESS_SETUP +=	${PROG}

REGRESS_TARGETS +=	run-default
run-default:
	time ${KTRACE} ./${PROG}

REGRESS_TARGETS +=	run-sock100
run-sock100:
	ulimit -n 500; time ${KTRACE} ./${PROG} -n100

REGRESS_TARGETS +=	run-splice
run-splice:
	time ${KTRACE} ./${PROG} -n10 -S2

REGRESS_TARGETS +=	run-unsplice
run-unsplice:
	time ${KTRACE} ./${PROG} -n10 -S2 -U1

REGRESS_TARGETS +=	run-max
run-max:
	time ${KTRACE} ./${PROG} -n10 -S4 -M100 -s2 -r2

REGRESS_TARGETS +=	run-idle
run-idle:
	time ${KTRACE} ./${PROG} -n10 -S4 -I100 -s2 -r2

.include <bsd.regress.mk>
