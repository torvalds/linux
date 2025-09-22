#	$OpenBSD: Makefile,v 1.16 2023/05/24 09:15:14 tb Exp $

PROGS += cipher_list
PROGS += ssl_get_shared_ciphers
PROGS += ssl_methods
PROGS += ssl_set_alpn_protos
PROGS += ssl_verify_param
PROGS += ssl_versions
PROGS += tls_ext_alpn
PROGS += tls_prf

WARNINGS=	Yes
LDADD =		${SSL_INT} -lcrypto
DPADD =		${LIBSSL} ${LIBCRYPTO}
CFLAGS+=	-DLIBRESSL_INTERNAL -Wall -Wundef -Werror
CFLAGS+=	-DCERTSDIR=\"${.CURDIR}/../certs\"
CFLAGS+=	-I${.CURDIR}/../../../../lib/libssl

LDADD_ssl_verify_param = ${LIBSSL} ${CRYPTO_INT}

.include <bsd.regress.mk>
