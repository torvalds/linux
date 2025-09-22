#	$OpenBSD: Makefile,v 1.3 2020/12/17 14:06:42 bluhm Exp $

DIR=	${.CURDIR}/../../netinet6/autoport

REGRESS_TARGETS=	run-regress-autoport
REGRESS_ROOT_TARGETS=	${REGRESS_TARGETS}

run-regress-autoport:
	${SUDO} /usr/bin/perl ${DIR}/autoport.pl 4 30000 31000 64

.include <bsd.regress.mk>
