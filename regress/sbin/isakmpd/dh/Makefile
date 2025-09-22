#	$OpenBSD: Makefile,v 1.3 2017/07/03 18:19:55 bluhm Exp $
#	$EOM: Makefile,v 1.10 2000/04/07 20:19:43 niklas Exp $

# Test DH:

PROG=		dhtest
SRCS=		dh.c dhtest.c
OS!=		awk '/^OS=/ { print $$2 }' ${.CURDIR}/../../Makefile
FEATURES!=	awk '/^FEATURES=/ { print $$0 }' ${.CURDIR}/../../Makefile | sed 's/FEATURES=.//'
CFLAGS+=	-DUSE_EC
LDADD+=		-lcrypto
DPADD+=		${LIBCRYPTO}

.if ${FEATURES:Mgmp} == "gmp"
CFLAGS+=	-DMP_FLAVOUR=MP_FLAVOUR_GMP
LDADD+=		-lgmp
DPADD+=		${LIBGMP}
.else
CFLAGS+=	-DMP_FLAVOUR=MP_FLAVOUR_OPENSSL
.endif

.include <bsd.regress.mk>
