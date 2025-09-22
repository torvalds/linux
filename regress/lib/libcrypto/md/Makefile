#	$OpenBSD: Makefile,v 1.2 2025/05/22 03:24:47 joshua Exp $

PROG =		md_test
LDADD =		-lcrypto
DPADD =		${LIBCRYPTO}
WARNINGS =	Yes
CFLAGS +=	-DLIBRESSL_INTERNAL -Werror
CFLAGS +=	-I${.CURDIR}/../test
SRCS +=		md_test.c
SRCS +=		test.c
SRCS +=		test_util.c

.PATH: ${.CURDIR}/../test

.include <bsd.regress.mk>
