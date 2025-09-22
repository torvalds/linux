#	$OpenBSD: Makefile,v 1.7 2022/12/03 09:59:19 tb Exp $

PROG =		rfc3779
LDADD =		-lcrypto
DPADD =		${LIBCRYPTO}
WARNINGS =	Yes
CFLAGS +=	-Werror

.include <bsd.regress.mk>
