#	$OpenBSD: Makefile,v 1.7 2024/03/30 00:34:40 jsing Exp $

PROG=		dsatest
LDADD=		-lcrypto
DPADD=		${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
