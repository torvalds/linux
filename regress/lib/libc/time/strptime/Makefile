#	$OpenBSD: Makefile,v 1.3 2012/12/16 18:28:53 jasper Exp $

PROG=		strptime_test
SRCS=		main.c
CLEANFILES+=	got

REGRESS_TARGETS=do-reg
do-reg: ${PROG}
	./${PROG} < ${.CURDIR}/tests > got
	diff -u ${.CURDIR}/expected got

.include <bsd.regress.mk>
