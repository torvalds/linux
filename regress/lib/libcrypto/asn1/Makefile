#	$OpenBSD: Makefile,v 1.28 2024/02/29 20:03:47 tb Exp $

PROGS = \
	asn1api \
	asn1basic \
	asn1complex \
	asn1evp \
	asn1object \
	asn1oct \
	asn1string_copy \
	asn1_string_to_utf8 \
	asn1time \
	asn1x509 \
	rfc5280time \
	x509_algor

DPADD+=		${LIBCRYPTO}
WARNINGS=	Yes
LDFLAGS+=	-lcrypto
CFLAGS+=	-DLIBRESSL_INTERNAL
CFLAGS+=	-Wall -Wundef -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libcrypto/asn1
CFLAGS+=	-I${.CURDIR}/../../../../lib/libcrypto/bytestring

LDADD_asn1basic = ${CRYPTO_INT}
LDADD_asn1object = ${CRYPTO_INT}
LDADD_asn1time = ${CRYPTO_INT}
LDADD_x509_algor = ${CRYPTO_INT}

.include <bsd.regress.mk>
