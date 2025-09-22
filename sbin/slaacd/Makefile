#	$OpenBSD: Makefile,v 1.3 2022/08/29 17:00:30 deraadt Exp $

PROG=	slaacd
SRCS=	control.c engine.c frontend.c log.c slaacd.c

MAN=	slaacd.8

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

# Don't compile slaacd as static binary by default
LDSTATIC=       
