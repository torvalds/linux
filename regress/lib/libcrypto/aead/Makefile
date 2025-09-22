#	$OpenBSD: Makefile,v 1.8 2022/08/20 19:25:14 jsing Exp $

PROG=	aeadtest
LDADD=	${CRYPTO_INT}
DPADD=	${LIBCRYPTO}
WARNINGS=	Yes
CFLAGS+=	-DLIBRESSL_INTERNAL -Werror

REGRESS_TARGETS=regress-aeadtest

regress-aeadtest: ${PROG}
	./${PROG} aead ${.CURDIR}/aeadtests.txt
	./${PROG} aes-128-gcm ${.CURDIR}/aes_128_gcm_tests.txt
	./${PROG} aes-192-gcm ${.CURDIR}/aes_192_gcm_tests.txt
	./${PROG} aes-256-gcm ${.CURDIR}/aes_256_gcm_tests.txt
	./${PROG} chacha20-poly1305 ${.CURDIR}/chacha20_poly1305_tests.txt
	./${PROG} xchacha20-poly1305 ${.CURDIR}/xchacha20_poly1305_tests.txt

.include <bsd.regress.mk>
