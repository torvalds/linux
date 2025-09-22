#	$OpenBSD: Makefile,v 1.2 2025/05/04 11:04:02 tb Exp $

PROG=	pq_test
SRC=	${.CURDIR}/../../../../lib/libssl
CFLAGS+=	-I${SRC}

LDADD=	${SSL_INT} -lcrypto
DPADD=	${LIBSSL} ${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
