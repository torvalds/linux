#	$OpenBSD: Makefile,v 1.2 2017/06/03 10:00:29 florian Exp $

PROG=	slaacctl
SRCS=	slaacctl.c parser.c

MAN=	slaacctl.8

CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../../sbin/slaacd
LDADD=	-lutil
DPADD=	${LIBUTIL}

.include <bsd.prog.mk>
