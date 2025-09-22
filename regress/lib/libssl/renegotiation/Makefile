#	$OpenBSD: Makefile,v 1.2 2025/02/01 12:26:50 jsing Exp $

PROG=	renegotiation_test
LDADD=	-lssl -lcrypto
DPADD=	${LIBSSL} ${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

REGRESS_TARGETS= \
	regress-renegotiation-test

regress-renegotiation-test: ${PROG}
	./renegotiation_test \
	    ${.CURDIR}/../../libssl/certs/server1-rsa.pem \
	    ${.CURDIR}/../../libssl/certs/server1-rsa-chain.pem \
	    ${.CURDIR}/../../libssl/certs/ca-root-rsa.pem

.include <bsd.regress.mk>
