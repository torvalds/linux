#	$OpenBSD: Makefile,v 1.1 2018/07/10 16:39:54 florian Exp $

PROG=	rad
SRCS=	control.c engine.c frontend.c log.c rad.c parse.y printconf.c

MAN=	rad.8 rad.conf.5

#DEBUG=	-g -DDEBUG=3 -O0
CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
YFLAGS=
LDADD+=	-levent -lutil
DPADD+= ${LIBEVENT} ${LIBUTIL}

.include <bsd.prog.mk>
