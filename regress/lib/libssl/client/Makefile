#	$OpenBSD: Makefile,v 1.6 2024/07/20 18:37:38 tb Exp $

PROG=	clienttest
LDADD=	${SSL_INT} -lcrypto
DPADD=	${LIBSSL} ${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
