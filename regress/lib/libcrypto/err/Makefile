#	$OpenBSD: Makefile,v 1.1 2024/10/02 14:41:46 jsing Exp $

PROG =		err_test
LDADD =		-lcrypto
DPADD =		${LIBCRYPTO}
WARNINGS =	Yes
CFLAGS +=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
