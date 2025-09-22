#	$OpenBSD: Makefile,v 1.4 2022/09/03 17:43:14 tb Exp $

PROG=	rc4_test
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
