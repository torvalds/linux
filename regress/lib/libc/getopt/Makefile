# $OpenBSD: Makefile,v 1.1 2020/03/23 03:01:21 schwarze Exp $

REGRESS_TARGETS =	getopt
PROG =			getopt-test
MAN =

getopt: ${PROG}
	sh ${.CURDIR}/getopt.sh

.include <bsd.regress.mk>
