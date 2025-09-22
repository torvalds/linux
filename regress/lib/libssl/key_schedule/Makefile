# $OpenBSD: Makefile,v 1.2 2022/06/29 15:06:18 tb Exp $

PROG=	key_schedule
LDADD=	${SSL_INT} -lcrypto
DPADD=	${LIBCRYPTO} ${LIBSSL}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Wundef -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libssl

.include <bsd.regress.mk>
