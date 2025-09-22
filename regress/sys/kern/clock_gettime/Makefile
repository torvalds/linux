# $OpenBSD: Makefile,v 1.2 2019/06/17 08:19:30 anton Exp $

PROG=	monotonicrelapse
LDADD+=	-lpthread
DPADD+=	${LIBPTHREAD}
WARNINGS=	yes

.if ! (make(clean) || make(cleandir) || make(obj))
NCPUONLINE!=	sysctl -n hw.ncpuonline
.endif

REGRESS_TARGETS+=	run-regress-monotonicrelapse

run-regress-monotonicrelapse: ${PROG}
	./${PROG} ${NCPUONLINE}

.include <bsd.regress.mk>
