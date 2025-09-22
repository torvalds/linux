#	$OpenBSD: Makefile,v 1.6 2022/06/29 15:06:18 tb Exp $

PROG=	bytestringtest
LDADD=	${SSL_INT} -lcrypto
DPADD=	${LIBCRYPTO} ${LIBSSL}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Wundef -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libssl

.include <bsd.regress.mk>
