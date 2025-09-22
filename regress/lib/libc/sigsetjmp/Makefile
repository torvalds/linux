#	$OpenBSD: Makefile,v 1.5 2002/09/02 20:01:43 avsm Exp $

PROG=	sigsetjmptest
SRCS=	jmptest.c

CFLAGS+= -DTEST_SIGSETJMP

.PATH:	${.CURDIR}/../setjmp

REGRESS_TARGETS=sigsetjmp-save sigsetjmp-nosave

sigsetjmp-save: ${PROG}
	./${PROG} save
sigsetjmp-nosave: ${PROG}
	./${PROG} nosave

.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
