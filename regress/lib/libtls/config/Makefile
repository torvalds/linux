#	$OpenBSD: Makefile,v 1.2 2018/02/08 10:05:43 jsing Exp $

PROG=	configtest
LDADD=	-lcrypto -lssl -ltls
DPADD=	${LIBCRYPTO} ${LIBSSL} ${LIBTLS}

WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Wall -Wundef -Werror

.include <bsd.regress.mk>
