#	$OpenBSD: Makefile,v 1.3 2017/05/07 04:18:47 jsg Exp $

.include <bsd.own.mk>

PROG=	ocspcheck
MAN=	ocspcheck.8

LDADD=	-ltls -lssl -lcrypto
DPADD=	${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

CFLAGS+= -Wall
CFLAGS+= -Wformat
CFLAGS+= -Wformat-security
CFLAGS+= -Wimplicit
CFLAGS+= -Wreturn-type
CFLAGS+= -Wshadow
CFLAGS+= -Wtrigraphs
CFLAGS+= -Wuninitialized
CFLAGS+= -Wunused
 .if ${COMPILER_VERSION:L} == "gcc4"
CFLAGS+= -Werror
.endif
CFLAGS+= -DLIBRESSL_INTERNAL

SRCS=	ocspcheck.c http.c

.include <bsd.prog.mk>
