#	$OpenBSD: Makefile,v 1.5 2017/07/03 18:19:55 bluhm Exp $
#	$EOM: Makefile,v 1.7 2000/03/28 21:22:06 ho Exp $

# Test Crypto:

PROG=		cryptotest
SRCS=		crypto.c cryptotest.c conf.c log.c monitor.c util.c
CFLAGS+=	-DUSE_TRIPLEDES -DUSE_CAST -DUSE_BLOWFISH -DUSE_DES \
		-DUSE_AES
LDADD+=		-lcrypto
DPADD+=		${LIBCRYPTO}

.include <bsd.regress.mk>
