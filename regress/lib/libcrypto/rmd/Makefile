#	$OpenBSD: Makefile,v 1.4 2022/09/02 15:45:52 tb Exp $

PROG=	rmd_test
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
