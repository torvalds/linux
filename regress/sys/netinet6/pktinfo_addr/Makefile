# $OpenBSD: Makefile,v 1.4 2021/10/05 08:08:58 anton Exp $

PROG=	runtest

REGRESS_TARGETS+=	run-regress-runtest
REGRESS_ROOT_TARGETS=	${REGRESS_TARGETS}

run-regress-runtest: ${PROG}
	${SETENV} PROG=${.OBJDIR}/${PROG} SUDO="${SUDO}" /bin/ksh ${.CURDIR}/test_wrapper.sh

.include <bsd.regress.mk>
