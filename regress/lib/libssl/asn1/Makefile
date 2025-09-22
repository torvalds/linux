#	$OpenBSD: Makefile,v 1.2 2021/06/30 18:09:46 jsing Exp $

PROG=	asn1test
LDADD=	-lcrypto -lssl
DPADD=	${LIBCRYPTO} ${LIBSSL}

WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libssl

.include <bsd.regress.mk>
