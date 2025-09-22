# $OpenBSD: Makefile,v 1.1 2022/02/11 16:34:23 jsing Exp $

PROG=		objectstest
LDADD=		-lcrypto
DPADD=		${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
