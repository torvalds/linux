#	$OpenBSD: Makefile,v 1.6 2025/05/22 03:35:40 joshua Exp $

PROG =		sha_test
LDADD =		-lcrypto
DPADD =		${LIBCRYPTO}
WARNINGS =	Yes
CFLAGS +=	-DLIBRESSL_INTERNAL -Werror
CFLAGS +=	-I${.CURDIR}/../test
SRCS +=		sha_test.c
SRCS +=		test.c
SRCS +=		test_util.c

.PATH: ${.CURDIR}/../test

.include <bsd.regress.mk>
