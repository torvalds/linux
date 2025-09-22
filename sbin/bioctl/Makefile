#	$OpenBSD: Makefile,v 1.14 2017/07/09 21:23:19 espie Exp $

PROG=	bioctl

LDADD=	-lutil
DPADD=	${LIBUTIL}

CDIAGFLAGS=     -Wall
CDIAGFLAGS+=    -Wpointer-arith
CDIAGFLAGS+=    -Wno-uninitialized
CDIAGFLAGS+=    -Wstrict-prototypes
CDIAGFLAGS+=    -Wmissing-prototypes
CDIAGFLAGS+=    -Wunused
CDIAGFLAGS+=    -Wsign-compare
CDIAGFLAGS+=    -Wshadow

MAN=	bioctl.8

.include <bsd.prog.mk>
