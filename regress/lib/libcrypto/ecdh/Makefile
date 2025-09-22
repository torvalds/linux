#	$OpenBSD: Makefile,v 1.6 2023/07/15 19:51:13 tb Exp $

PROGS =		ecdhtest
PROGS +=	ecc_cdh

LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
