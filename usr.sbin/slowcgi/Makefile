#	$OpenBSD: Makefile,v 1.2 2014/12/05 20:02:41 florian Exp $

PROG=	slowcgi
SRCS=	slowcgi.c
CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
LDADD=	-levent
DPADD=	${LIBEVENT}
MAN=	slowcgi.8
.include <bsd.prog.mk>
