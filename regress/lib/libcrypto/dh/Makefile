#	$OpenBSD: Makefile,v 1.5 2022/01/14 09:38:50 tb Exp $

PROG=	dhtest
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
