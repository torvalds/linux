#	$OpenBSD: Makefile,v 1.1 2023/12/27 12:34:32 jsing Exp $

PROG =		exdata_test
LDADD =		-lcrypto
DPADD =		${LIBCRYPTO}
WARNINGS =	Yes
CFLAGS +=	-DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
