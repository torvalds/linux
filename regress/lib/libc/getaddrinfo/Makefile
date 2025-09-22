#	$OpenBSD: Makefile,v 1.5 2017/07/27 17:43:24 bluhm Exp $

PROG =		gaitest
CLEANFILES =	out

run-regress-${PROG}: ${PROG}
	sh ${.CURDIR}/testsuite.sh >out 2>&1
	diff -u10 out ${.CURDIR}/answer

.include <bsd.regress.mk>
