#	$OpenBSD: Makefile,v 1.1 2022/11/05 21:58:24 jsing Exp $

PROG=		exportertest
LDADD=		${SSL_INT} -lcrypto
DPADD=		${LIBSSL} ${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libssl

.include <bsd.regress.mk>
