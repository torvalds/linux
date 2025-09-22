#	$OpenBSD: Makefile,v 1.2 2024/03/20 10:38:05 jsing Exp $

PROG=	keypairtest
LDADD=	-lcrypto -lssl ${TLS_INT}
DPADD=	${LIBCRYPTO} ${LIBSSL} ${LIBTLS}

WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Wall -Wundef -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libtls

REGRESS_TARGETS= \
	regress-keypairtest

regress-keypairtest: ${PROG}
	./keypairtest \
	    ${.CURDIR}/../../libssl/certs/ca-root-rsa.pem \
	    ${.CURDIR}/../../libssl/certs/server1-rsa.pem \
	    ${.CURDIR}/../../libssl/certs/server1-rsa.pem

.include <bsd.regress.mk>
