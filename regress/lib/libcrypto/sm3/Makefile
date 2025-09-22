#	$OpenBSD: Makefile,v 1.1 2018/11/11 07:12:33 tb Exp $

PROG=	sm3test
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
