# $OpenBSD: Makefile,v 1.6 2023/04/14 12:37:20 tb Exp $

PROG=		cttest
LDADD=		-lcrypto
DPADD=		${LIBCRYPTO}

WARNINGS=	Yes
CFLAGS+=	-Wundef -Werror
CFLAGS+=	-DLIBRESSL_CRYPTO_INTERNAL -DLIBRESSL_INTERNAL
CFLAGS+=	-DCTPATH=\"${.CURDIR}\"

.include <bsd.regress.mk>
