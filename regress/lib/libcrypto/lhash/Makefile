#	$OpenBSD: Makefile,v 1.1 2024/05/06 14:31:25 jsing Exp $

PROG =	lhash_test

DPADD+=		${LIBCRYPTO}
WARNINGS=	Yes
LDFLAGS+=	-lcrypto
CFLAGS+=	-DLIBRESSL_INTERNAL
CFLAGS+=	-Wall -Wundef -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libcrypto

.include <bsd.regress.mk>
