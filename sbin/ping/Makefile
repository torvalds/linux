#	$OpenBSD: Makefile,v 1.9 2016/09/17 15:03:39 florian Exp $

PROG=	ping

CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual

MAN=	ping.8

LDADD=	-lm
DPADD=	${LIBM}

LINKS=	${BINDIR}/ping ${BINDIR}/ping6
BINOWN=	root
BINMODE=4555

.include <bsd.prog.mk>
