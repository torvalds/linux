#	$OpenBSD: Makefile,v 1.6 2020/05/18 17:52:18 denis Exp $

.PATH:		${.CURDIR}/../ospfd

PROG=	ospfctl
SRCS=	logmsg.c ospfctl.c output.c parser.c
CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../ospfd
LDADD=	-lutil
DPADD=	${LIBUTIL}
MAN=	ospfctl.8

.include <bsd.prog.mk>
