#	$OpenBSD: Makefile,v 1.1 2019/01/17 06:46:10 jsing Exp $

PROG=		buffertest
LDADD=		${SSL_INT} -lcrypto
DPADD=		${LIBSSL} ${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Wall -Wundef -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libssl

.include <bsd.regress.mk>
