#	$OpenBSD: Makefile,v 1.1 2015/10/22 15:47:00 claudio Exp $

PROG=		ifnitest
SRCS=		ifnitest.c
NOMAN=		# defined

REGRESS_TARGETS=do-test

do-test: ${PROG}
	./${PROG}

.include <bsd.regress.mk>
