#       $OpenBSD: Makefile,v 1.1 2022/11/07 17:41:40 joshua Exp $

PROG=   aes_test
LDADD=  -lcrypto
DPADD=  ${LIBCRYPTO}
WARNINGS=       Yes
CFLAGS+=        -DLIBRESSL_INTERNAL -DLIBRESSL_INTERNAL -Werror

.include <bsd.regress.mk>
