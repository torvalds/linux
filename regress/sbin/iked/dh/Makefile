#	$OpenBSD: Makefile,v 1.4 2021/05/28 21:09:01 tobhe Exp $

# Test DH:

PROG=		dhtest
SRCS=		dh.c dhtest.c smult_curve25519_ref.c imsg_util.c
SRCS+=		sntrup761.c crypto_hash.c
TOPSRC=		${.CURDIR}/../../../../sbin/iked
TOPOBJ!=	cd ${TOPSRC}; printf "all:\n\t@pwd\n" |${MAKE} -f-
.PATH:		${TOPSRC} ${TOPOBJ}
CFLAGS+=	-I${TOPSRC} -I${TOPOBJ} -Wall

NOMAN=
LDADD+=		-lcrypto -lutil
DPADD+=		${LIBCRYPTO}
DEBUG=		-g

.include <bsd.regress.mk>
