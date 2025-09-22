#	$OpenBSD: Makefile,v 1.4 2017/01/20 12:19:18 benno Exp $

.PATH:		${.CURDIR}/../ldpd

PROG=	ldpctl
SRCS=	util.c log.c logmsg.c ldpctl.c parser.c
CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../ldpd
LDADD=	-lutil
DPADD=	${LIBUTIL}
MAN=	ldpctl.8

.include <bsd.prog.mk>
