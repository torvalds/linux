#	$OpenBSD: Makefile,v 1.4 2022/09/06 15:36:25 tb Exp $

PROG=	rc2_test
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
