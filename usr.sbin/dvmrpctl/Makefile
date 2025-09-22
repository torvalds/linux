#	$OpenBSD: Makefile,v 1.5 2016/09/02 16:20:47 benno Exp $

.PATH:		${.CURDIR}/../dvmrpd

PROG=	dvmrpctl
SRCS=	log.c logmsg.c dvmrpctl.c parser.c

CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../dvmrpd

LDADD=	-lutil
DPADD=	${LIBUTIL}

MAN=	dvmrpctl.8

.include <bsd.prog.mk>
