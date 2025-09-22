#	$OpenBSD: Makefile,v 1.7 2021/12/26 08:37:21 anton Exp $

PROG=	utf8test
CPPFLAGS+=-I${.CURDIR}/../../../../lib/libcrypto/asn1
CPPFLAGS+=-I${.CURDIR}/../../../../lib/libcrypto/bytestring
LDADD=	${CRYPTO_INT}
DPADD=	${LIBCRYPTO}
WARNINGS=Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
