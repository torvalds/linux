#	$OpenBSD: Makefile,v 1.1 2019/03/17 17:48:31 tb Exp $

PROG =		sm4test
LDADD =		-lcrypto
DPADD =		${LIBCRYPTO}
WARNINGS =	Yes
CFLAGS +=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
