#	$OpenBSD: Makefile,v 1.4 2016/09/02 14:06:35 benno Exp $

.PATH:		${.CURDIR}/../ospf6d

PROG=	ospf6ctl
SRCS=	log.c logmsg.c ospf6ctl.c parser.c util.c
CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../ospf6d
LDADD=	-lutil
DPADD=	${LIBUTIL}
MAN=	ospf6ctl.8

.include <bsd.prog.mk>
