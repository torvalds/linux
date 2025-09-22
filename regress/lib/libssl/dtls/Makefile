#	$OpenBSD: Makefile,v 1.4 2024/03/20 10:38:05 jsing Exp $

PROG=		dtlstest
LDADD=		${SSL_INT} -lcrypto
DPADD=		${LIBSSL} ${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libcrypto/bio
CFLAGS+=	-I${.CURDIR}/../../../../lib/libssl

REGRESS_TARGETS= \
	regress-dtlstest

# XXX(jsing): use CA root and chain
regress-dtlstest: ${PROG}
	./dtlstest \
	    ${.CURDIR}/../../libssl/certs/server1-rsa.pem \
	    ${.CURDIR}/../../libssl/certs/server1-rsa.pem \
	    ${.CURDIR}/../../libssl/certs/ca-int-rsa.pem

.include <bsd.regress.mk>
