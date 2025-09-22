#	$OpenBSD: Makefile,v 1.5 2017/04/16 10:16:35 jsg Exp $

PROG=	tftp-proxy
SRCS=	tftp-proxy.c filter.c
MAN=	tftp-proxy.8
LDADD=	-levent
DPADD=	${LIBEVENT}

CFLAGS+=-Wall
CDIAGFLAGS=

.include <bsd.prog.mk>
