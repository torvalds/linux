#	$OpenBSD: Makefile,v 1.2 2020/09/13 16:51:30 jsing Exp $

PROG=	cipherstest
LDADD=	${SSL_INT} -lcrypto
DPADD=	${LIBSSL} ${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
