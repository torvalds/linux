#	$OpenBSD: Makefile,v 1.4 2017/06/22 18:07:31 bluhm Exp $

DIR=${.CURDIR}/../../../../sys

PROG=	gmac_test
SRCS+=	aes.c gmac.c gmac_test.c
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

.PATH:	${DIR}/crypto

.include <bsd.regress.mk>
