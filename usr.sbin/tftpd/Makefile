#	$OpenBSD: Makefile,v 1.6 2017/04/16 10:16:35 jsg Exp $

PROG=	tftpd
SRCS=	tftpd.c
LDADD=	-levent
DPADD=	${LIBEVENT}
CFLAGS+= -Wall
CDIAGFLAGS=

MAN=	tftpd.8

.include <bsd.prog.mk>
