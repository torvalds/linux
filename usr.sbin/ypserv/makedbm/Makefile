#	$OpenBSD: Makefile,v 1.3 1997/07/22 10:52:59 maja Exp $

PROG=	makedbm
SRCS=	makedbm.c ypdb.c db.c
MAN=	makedbm.8
.PATH:	${.CURDIR}/../common
CFLAGS+=-I${.CURDIR}/../common

.include <bsd.prog.mk>
