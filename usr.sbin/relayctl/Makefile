#	$OpenBSD: Makefile,v 1.8 2015/11/21 12:37:42 reyk Exp $

.PATH:		${.CURDIR}/../relayd

PROG=		relayctl
SRCS=		log.c util.c relayctl.c parser.c

MAN=		relayctl.8

LDADD=		-lutil
DPADD=		${LIBUTIL}
CFLAGS+=	-Wall -I${.CURDIR} -I${.CURDIR}/../relayd
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare
CDIAGFLAGS=

.include <bsd.prog.mk>
