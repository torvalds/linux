#	$OpenBSD: Makefile,v 1.1 2018/07/10 22:12:43 florian Exp $

PROG=	ractl
SRCS=	ractl.c parser.c

MAN=	ractl.8

CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../rad
LDADD=	-lutil
DPADD=	${LIBUTIL}

.include <bsd.prog.mk>
