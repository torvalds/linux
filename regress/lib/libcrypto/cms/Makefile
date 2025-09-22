#	$OpenBSD: Makefile,v 1.1 2019/11/02 15:35:10 jsing Exp $

PROG=	cmstest
LDADD=	-lcrypto
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
