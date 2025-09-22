# $OpenBSD: Makefile,v 1.3 2024/03/20 10:38:05 jsing Exp $

PROG=	tlstest
LDADD=	-lcrypto -lssl -ltls
DPADD=	${LIBCRYPTO} ${LIBSSL} ${LIBTLS}

WARNINGS=	Yes
CFLAGS+=	-Werror

REGRESS_TARGETS= \
	regress-tlstest

regress-tlstest: ${PROG}
	./tlstest \
	    ${.CURDIR}/../../libssl/certs/ca-root-rsa.pem \
	    ${.CURDIR}/../../libssl/certs/server1-rsa-chain.pem \
	    ${.CURDIR}/../../libssl/certs/server1-rsa.pem

.include <bsd.regress.mk>
