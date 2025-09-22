#	$OpenBSD: Makefile,v 1.5 2014/07/09 11:43:01 jsing Exp $

PROG=	gcm128test
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
