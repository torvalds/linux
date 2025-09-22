#	$OpenBSD: Makefile,v 1.2 2016/09/02 18:00:07 benno Exp $

.PATH:		${.CURDIR}/../eigrpd

PROG=	eigrpctl
SRCS=	util.c log.c logmsg.c eigrpctl.c parser.c
CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../eigrpd
LDADD=	-lutil
DPADD=	${LIBUTIL}
MAN=	eigrpctl.8

.include <bsd.prog.mk>
