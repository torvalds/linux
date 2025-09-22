#	$OpenBSD: Makefile,v 1.5 2021/04/10 10:19:19 eric Exp $

.PATH:	${.CURDIR}/..

PROG=	smtp

BINDIR=	/usr/bin
MAN=	smtp.1

SRCS+=	iobuf.c
SRCS+=	ioev.c
SRCS+=	log.c
SRCS+=	smtp_client.c
SRCS+=	smtpc.c

CPPFLAGS+= -DIO_TLS

LDADD+=	-levent -lutil -ltls -lssl -lcrypto
DPADD+=	${LIBEVENT} ${LIBUTIL} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

.include <bsd.prog.mk>
