#	$OpenBSD: Makefile,v 1.4 2017/06/22 18:07:31 bluhm Exp $

DIR=${.CURDIR}/../../../../sys

PROG=	hmac_test
SRCS+=	md5.c sha1.c sha2.c hmac.c hmac_test.c
CDIAGFLAGS=	-Wall
CDIAGFLAGS+=	-Werror
CDIAGFLAGS+=	-Wpointer-arith
CDIAGFLAGS+=	-Wno-uninitialized
CDIAGFLAGS+=	-Wstrict-prototypes
CDIAGFLAGS+=	-Wmissing-prototypes
CDIAGFLAGS+=	-Wunused
CDIAGFLAGS+=	-Wsign-compare
#CDIAGFLAGS+=	-Wshadow

CFLAGS+=	-I${DIR}

.PATH:	${DIR}/crypto/

run-regress-${PROG}: ${PROG}
	./${PROG} | diff - ${.CURDIR}/${PROG}.txt

.include <bsd.regress.mk>
