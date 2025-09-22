# $OpenBSD: Makefile,v 1.7 2016/09/11 14:31:02 natano Exp $

.PATH:		${.CURDIR}/../../sbin/iked

PROG=		ikectl
SRCS=		log.c ikeca.c ikectl.c parser.c util.c

MAN=		ikectl.8

LDADD=		-lutil -lcrypto
DPADD=		${LIBUTIL} ${LIBCRYPTO}
CFLAGS+=	-Wall -I${.CURDIR} -I${.CURDIR}/../../sbin/iked
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare

distribution:
	${INSTALL} -C -o root -g wheel -m 0644 ${.CURDIR}/ikeca.cnf \
	    ${DESTDIR}/etc/ssl/ikeca.cnf

.include <bsd.prog.mk>
