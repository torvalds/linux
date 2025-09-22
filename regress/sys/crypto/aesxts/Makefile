#       $OpenBSD: Makefile,v 1.7 2017/06/22 18:07:31 bluhm Exp $

DIR=	${.CURDIR}/../../../../sys

CFLAGS+=	-I${DIR}

PROG=   aes_xts
SRCS=	aes_xts.c

CDIAGFLAGS=	-Wall
CDIAGFLAGS+=	-Werror
CDIAGFLAGS+=	-Wpointer-arith
CDIAGFLAGS+=	-Wno-uninitialized
CDIAGFLAGS+=	-Wstrict-prototypes
CDIAGFLAGS+=	-Wmissing-prototypes
CDIAGFLAGS+=	-Wunused
CDIAGFLAGS+=	-Wsign-compare
CDIAGFLAGS+=	-Wshadow

.PATH:  ${DIR}/crypto
SRCS+=	cast.c ecb_enc.c ecb3_enc.c gmac.c aes.c set_key.c
SRCS+=	rijndael.c chachapoly.c poly1305.c
SRCS+=	xform.c

.include <bsd.regress.mk>
