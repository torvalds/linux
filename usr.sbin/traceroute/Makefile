#	$OpenBSD: Makefile,v 1.11 2021/09/03 09:13:00 florian Exp $

PROG=	traceroute

SRCS=	traceroute.c worker.c

CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
LDADD=	-levent
DPADD=	${LIBEVENT}

MAN=	traceroute.8

LINKS=	${BINDIR}/traceroute ${BINDIR}/traceroute6
BINOWN=	root
BINMODE=4555

.include <bsd.prog.mk>
