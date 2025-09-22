#	$OpenBSD: Makefile,v 1.5 2022/07/07 11:40:17 tb Exp $

PROG=	ssltest
LDADD=	-lcrypto -lssl
DPADD=	${LIBCRYPTO} ${LIBSSL}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror
CFLAGS+=	-I${.CURDIR}/../../../../lib/libssl

REGRESS_TARGETS=regress-ssltest

regress-ssltest: ${PROG}
	sh ${.CURDIR}/testssl \
	  ${.CURDIR}/../certs/server1-rsa.pem ${.CURDIR}/../certs/server1-rsa-chain.pem \
	  ${.CURDIR}/../certs/ca-root-rsa.pem

.include <bsd.regress.mk>
