#	$OpenBSD: Makefile,v 1.3 2016/09/02 14:07:52 benno Exp $

.PATH:		${.CURDIR}/../ripd

PROG=	ripctl
SRCS=	logmsg.c ripctl.c parser.c
CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../ripd
LDADD=	-lutil
DPADD=	${LIBUTIL}
MAN=	ripctl.8

.include <bsd.prog.mk>
