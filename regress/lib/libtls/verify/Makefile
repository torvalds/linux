#	$OpenBSD: Makefile,v 1.3 2017/01/09 12:34:03 jsing Exp $

PROG=	verifytest
LDADD=	-lcrypto -lssl ${TLS_INT}
DPADD=	${LIBCRYPTO} ${LIBSSL} ${LIBTLS}

WARNINGS=	Yes
CFLAGS+=	-Werror

.include <bsd.regress.mk>
