#	$OpenBSD: Makefile,v 1.1 2017/05/06 20:42:57 beck Exp $

PROG=	hkdf_test
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL

.include <bsd.regress.mk>
