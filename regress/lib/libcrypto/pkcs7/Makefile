#	$OpenBSD: Makefile,v 1.2 2014/07/08 15:53:53 jsing Exp $

PROG=	pkcs7test
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
