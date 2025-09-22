#	$OpenBSD: Makefile,v 1.4 2022/11/06 14:56:08 joshua Exp $

PROG=	bf_test
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
