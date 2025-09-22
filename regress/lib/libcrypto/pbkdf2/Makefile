#	$OpenBSD: Makefile,v 1.1 2014/07/11 20:37:59 miod Exp $

PROG=	pbkdf2
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
