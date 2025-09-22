#	$OpenBSD: Makefile,v 1.4 2023/04/30 04:55:30 tb Exp $

PROGS =		policy

LDADD =	${CRYPTO_INT}
DPADD =	${LIBCRYPTO}

WARNINGS =	Yes
CFLAGS +=	-DLIBRESSL_INTERNAL -Wall -Werror
CFLAGS +=	-I${.CURDIR}/../../../../../lib/libcrypto/x509
CFLAGS += 	-I${.CURDIR}/../../../../../lib/libcrypto/bytestring
CFLAGS += 	-DCERTSDIR=\"${.CURDIR}/../../../libcrypto/x509/policy\"

.include "../../Makefile.inc"
.include <bsd.regress.mk>
