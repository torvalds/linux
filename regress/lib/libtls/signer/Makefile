#	$OpenBSD: Makefile,v 1.4 2023/04/14 12:41:26 tb Exp $

PROG=	signertest
LDADD=	-lcrypto -lssl ${TLS_INT}
DPADD=	${LIBCRYPTO} ${LIBSSL} ${LIBTLS}

WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Wall -Wundef -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libtls/
CFLAGS+=	-DCERTSDIR=\"${.CURDIR}/../../libssl/certs\"

.include <bsd.regress.mk>
