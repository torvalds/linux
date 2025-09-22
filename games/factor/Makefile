#	$OpenBSD: Makefile,v 1.6 2016/07/11 18:30:21 tb Exp $

PROG=	factor
SRCS=	factor.c pattern.c pr_tbl.c
CFLAGS+=-I${.CURDIR}/../primes
MAN=	factor.6
.PATH:	${.CURDIR}/../primes

.include <bsd.prog.mk>
