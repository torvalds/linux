#	$OpenBSD: Makefile,v 1.7 2003/11/20 23:23:09 avsm Exp $

PROG=	rdate
SRCS=	rdate.c rfc868time.c ntp.c ntpleaps.c
CFLAGS+=-Wall
DPADD+= ${LIBUTIL}
LDADD+= -lutil

MAN=	rdate.8

.include <bsd.prog.mk>
