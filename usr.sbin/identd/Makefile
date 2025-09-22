#	$OpenBSD: Makefile,v 1.3 2017/04/16 10:16:35 jsg Exp $

PROG=	identd
LDADD=	-levent
DPADD=	${LIBEVENT}
CFLAGS+= -Wall

MAN=	identd.8

.include <bsd.prog.mk>
