#	$OpenBSD: Makefile,v 1.11 2015/02/07 10:45:19 henning Exp $

PROG=	spamd
SRCS=	spamd.c sdl.c gdcopy.c grey.c sync.c
MAN=	spamd.8

CFLAGS+= -Wall -Wstrict-prototypes

LDADD+=	-ltls -lssl -lcrypto
DPADD+=	${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

.include <bsd.prog.mk>
