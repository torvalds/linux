#	$OpenBSD: Makefile,v 1.3 2006/11/26 11:31:13 deraadt Exp $

PROG=	ftp-proxy
SRCS=	ftp-proxy.c filter.c
MAN=	ftp-proxy.8

CFLAGS+= -I${.CURDIR}
CFLAGS+= -Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith \
         -Wno-uninitialized
LDADD+=	-levent
DPADD+= ${LIBEVENT}

.include <bsd.prog.mk>
