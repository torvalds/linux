#	$OpenBSD: Makefile,v 1.2 2023/04/14 12:38:30 tb Exp $

PROG=		apitest
LDADD=		-lssl -lcrypto
DPADD=		${LIBSSL} ${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror
CFLAGS+=	-DCERTSDIR=\"${.CURDIR}/../../libssl/certs\"

.include <bsd.regress.mk>
