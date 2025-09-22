#	$OpenBSD: Makefile,v 1.1 2020/10/07 07:52:17 jsing Exp $

PROG=	tlslegacytest
LDADD=	-lssl -lcrypto
DPADD=	${LIBSSL} ${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
