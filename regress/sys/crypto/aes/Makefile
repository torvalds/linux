#       $OpenBSD: Makefile,v 1.5 2017/06/22 18:07:30 bluhm Exp $

DIR=	${.CURDIR}/../../../../sys

CFLAGS+=	-I${DIR}

PROG=   aestest
SRCS=	aestest.c
CDIAGFLAGS=	-Wall
#CDIAGFLAGS+=	-Werror
CDIAGFLAGS+=	-Wpointer-arith
CDIAGFLAGS+=	-Wno-uninitialized
CDIAGFLAGS+=	-Wstrict-prototypes
CDIAGFLAGS+=	-Wmissing-prototypes
CDIAGFLAGS+=	-Wunused
CDIAGFLAGS+=	-Wsign-compare
CDIAGFLAGS+=	-Wshadow

.PATH:	${DIR}/crypto
SRCS+=	aes.c

run-regress-${PROG}: ${PROG}
	./${PROG} ${.CURDIR}/vectors/*.txt

.include <bsd.regress.mk>
