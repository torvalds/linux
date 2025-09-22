#	$OpenBSD: Makefile,v 1.2 2024/08/11 13:05:43 jsing Exp $

PROG =	crypto_test

DPADD+=		${LIBCRYPTO}
WARNINGS=	Yes
LDFLAGS+=	-lcrypto
CFLAGS+=	-DLIBRESSL_INTERNAL
CFLAGS+=	-Wall -Wundef -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libcrypto
CFLAGS+=	-I${.CURDIR}/../../../../lib/libcrypto/arch/${MACHINE_CPU}

.include <bsd.regress.mk>
